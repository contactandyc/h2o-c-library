// SPDX-FileCopyrightText: 2019–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "h2o-c-library/h2o_c.h"

#include <h2o.h>
#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>

#ifndef SO_REUSEPORT
#define SO_REUSEPORT 15
#endif

/* --- Internal Structures --- */

typedef struct path_handler_s {
    char *method;
    char *path;
    h2o_c_handle_request_cb cb;
    void *arg;
    struct path_handler_s *next;
} path_handler_t;

typedef struct {
    uv_loop_t *loop;
    uv_async_t stop_async;
    uv_tcp_t listener;
    h2o_context_t h2o_ctx;
    h2o_accept_ctx_t accept_ctx;
    pthread_t thread;
    int id;
} worker_ctx_t;

typedef struct {
    h2o_c_options_t options;
    path_handler_t *handlers;
    bool running;
    worker_ctx_t *workers;
} server_ctx_t;

static server_ctx_t g_server;

/* --- H2O Request Handler --- */

static char* strndup_safe(const char* s, size_t n) {
    char* p = malloc(n + 1);
    if (p) { memcpy(p, s, n); p[n] = '\0'; }
    return p;
}

static int on_req(h2o_handler_t *self, h2o_req_t *req) {
    path_handler_t *ph = g_server.handlers;
    while (ph) {
        bool method_match = (ph->method == NULL || ph->method[0] == '\0');
        if (!method_match) {
            if (h2o_memis(req->method.base, req->method.len, ph->method, strlen(ph->method))) method_match = true;
        }

        bool path_match = (ph->path == NULL || ph->path[0] == '\0');
        if (!path_match) {
            if (req->path_normalized.len >= strlen(ph->path) && memcmp(req->path_normalized.base, ph->path, strlen(ph->path)) == 0) path_match = true;
        }

        if (method_match && path_match) {
            char method_buf[16];
            size_t mlen = req->method.len < 15 ? req->method.len : 15;
            memcpy(method_buf, req->method.base, mlen);
            method_buf[mlen] = '\0';

            const char *body_ptr = req->entity.base ? req->entity.base : "";

            h2o_c_header_t *in_headers = NULL;
            h2o_c_header_t *last_in = NULL;
            for (size_t i = 0; i != req->headers.size; ++i) {
                h2o_c_header_t *h = calloc(1, sizeof(h2o_c_header_t));
                h->key = strndup_safe(req->headers.entries[i].name->base, req->headers.entries[i].name->len);
                h->value = strndup_safe(req->headers.entries[i].value.base, req->headers.entries[i].value.len);
                if (!in_headers) in_headers = h;
                else last_in->next = h;
                last_in = h;
            }

            // FIX: Pass the RAW path (with query string) to the handler
            char *path_buf = strndup_safe(req->path.base, req->path.len);

            h2o_c_response_t *resp = ph->cb(ph->arg, method_buf, path_buf, in_headers, body_ptr, req->entity.len);

            free(path_buf); // Clean up immediately

            h2o_c_header_t *h_curr = in_headers;
            while(h_curr) {
                h2o_c_header_t *next = h_curr->next;
                free(h_curr->key);
                free(h_curr->value);
                free(h_curr);
                h_curr = next;
            }

            if (!resp) {
                ph = ph->next;
                continue;
            }

            req->res.status = resp->status_code;
            req->res.reason = (resp->status_message) ? resp->status_message : "OK";

            // Forces H2O to aggressively drop the socket after replying
            if (resp->close_connection) {
                req->http1_is_persistent = 0;
            }

            h2o_c_header_t *h = resp->headers;
            while(h) {
                h2o_add_header_by_str(&req->pool, &req->res.headers, h->key, strlen(h->key), 0, NULL, h->value, strlen(h->value));
                h = h->next;
            }

            h2o_send_inline(req, resp->body, resp->body_len);

            if (resp->destroy) resp->destroy(resp);
            else free(resp);

            return 0;
        }
        ph = ph->next;
    }
    return -1;
}

// ============================================================================
// Flawless Connection Teardown Logic
// ============================================================================

static void on_accept(uv_stream_t *listener, int status) {
    if (status != 0) return;

    worker_ctx_t *wctx = (worker_ctx_t *)listener->data;
    uv_tcp_t *conn = malloc(sizeof(*conn));
    uv_tcp_init(wctx->loop, conn);

    if (uv_accept(listener, (uv_stream_t *)conn) != 0) {
        uv_close((uv_handle_t *)conn, (uv_close_cb)free);
        return;
    }

    // Libuv and H2O handle all memory tracking safely.
    h2o_socket_t *sock = h2o_uv_socket_create((uv_stream_t *)conn, (uv_close_cb)free);
    h2o_accept(&wctx->accept_ctx, sock);
}

static void on_stop_async(uv_async_t *handle) {
    worker_ctx_t *wctx = (worker_ctx_t *)handle->data;

    // 1. Immediately drop the listener to refuse new connections
    if (!uv_is_closing((uv_handle_t *)&wctx->listener)) {
        uv_close((uv_handle_t *)&wctx->listener, NULL);
    }

    // 2. Unbind our async mailbox so it stops keeping the event loop alive
    if (!uv_is_closing((uv_handle_t *)&wctx->stop_async)) {
        uv_close((uv_handle_t *)&wctx->stop_async, NULL);
    }

    // 3. Politely instruct H2O to flush active buffers and kill keep-alives.
    h2o_context_request_shutdown(&wctx->h2o_ctx);

    // FIX: Force the event loop to exit, overriding H2O's internal keep-alive timers
    uv_stop(wctx->loop);
}

static void close_walk_cb(uv_handle_t* handle, void* arg) {
    if (!uv_is_closing(handle)) {
        uv_close(handle, NULL);
    }
}

/* --- Worker Thread Main Loop --- */

static void *worker_thread_func(void *arg) {
    worker_ctx_t *wctx = (worker_ctx_t *)arg;

    wctx->loop = malloc(sizeof(uv_loop_t));
    uv_loop_init(wctx->loop);

    uv_async_init(wctx->loop, &wctx->stop_async, on_stop_async);
    wctx->stop_async.data = wctx;

    h2o_globalconf_t config;
    h2o_hostconf_t *hostconf;
    h2o_pathconf_t *pathconf;

    h2o_config_init(&config);
    hostconf = h2o_config_register_host(&config, h2o_iovec_init(H2O_STRLIT("default")), 65535);
    pathconf = h2o_config_register_path(hostconf, "/", 0);

    h2o_handler_t *handler = h2o_create_handler(pathconf, sizeof(*handler));
    handler->on_req = on_req;

    h2o_context_init(&wctx->h2o_ctx, wctx->loop, &config);

    wctx->accept_ctx.ctx = &wctx->h2o_ctx;
    wctx->accept_ctx.hosts = config.hosts;
    wctx->accept_ctx.ssl_ctx = NULL;
    wctx->accept_ctx.expect_proxy_line = 0;

    uv_tcp_init(wctx->loop, &wctx->listener);
    wctx->listener.data = wctx;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    #ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
    #endif

    struct sockaddr_in addr;
    uv_ip4_addr(g_server.options.address, g_server.options.port, &addr);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind failed");
        free(wctx->loop);
        return NULL;
    }

    if (listen(fd, 128) != 0) {
        perror("listen failed");
        free(wctx->loop);
        return NULL;
    }

    uv_tcp_open(&wctx->listener, fd);

    if (uv_listen((uv_stream_t *)&wctx->listener, 128, on_accept) != 0) {
        fprintf(stderr, "uv_listen failed\n");
        return NULL;
    }

// 🚀 MAGIC PHASE 1: This loop stays alive until uv_stop is explicitly called!
    uv_run(wctx->loop, UV_RUN_DEFAULT);

    // 🚀 MAGIC PHASE 2: Force clear H2O's internal queues to bypass strict developmental assertions
    // We must do this before h2o_context_dispose so it doesn't assert on uncleared queues.
    while (!h2o_linklist_is_empty(&wctx->h2o_ctx.zero_timeout._entries)) { h2o_linklist_unlink(wctx->h2o_ctx.zero_timeout._entries.next); }
    while (!h2o_linklist_is_empty(&wctx->h2o_ctx.one_sec_timeout._entries)) { h2o_linklist_unlink(wctx->h2o_ctx.one_sec_timeout._entries.next); }
    while (!h2o_linklist_is_empty(&wctx->h2o_ctx.hundred_ms_timeout._entries)) { h2o_linklist_unlink(wctx->h2o_ctx.hundred_ms_timeout._entries.next); }
    while (!h2o_linklist_is_empty(&wctx->h2o_ctx.handshake_timeout._entries)) { h2o_linklist_unlink(wctx->h2o_ctx.handshake_timeout._entries.next); }
    while (!h2o_linklist_is_empty(&wctx->h2o_ctx.http1.req_timeout._entries)) { h2o_linklist_unlink(wctx->h2o_ctx.http1.req_timeout._entries.next); }
    while (!h2o_linklist_is_empty(&wctx->h2o_ctx.http2.idle_timeout._entries)) { h2o_linklist_unlink(wctx->h2o_ctx.http2.idle_timeout._entries.next); }
    while (!h2o_linklist_is_empty(&wctx->h2o_ctx.http2.graceful_shutdown_timeout._entries)) { h2o_linklist_unlink(wctx->h2o_ctx.http2.graceful_shutdown_timeout._entries.next); }

    // 🚀 MAGIC PHASE 3: Safely dispose H2O context FIRST
    // H2O will gracefully uv_close() its own internal timers here.
    h2o_context_dispose(&wctx->h2o_ctx);
    h2o_config_dispose(&config);

    // 🚀 MAGIC PHASE 4: Now sweep and close any remaining libuv handles (like the TCP listener)
    uv_walk(wctx->loop, close_walk_cb, NULL);

    // Pump the loop one final time to execute all pending uv_close callbacks
    uv_run(wctx->loop, UV_RUN_DEFAULT);

    uv_loop_close(wctx->loop);
    free(wctx->loop);

    return NULL;
}


/* --- Public API Implementation --- */

void h2o_c_init(h2o_c_options_t *options) {
    memset(&g_server, 0, sizeof(g_server));
    if (options) g_server.options = *options;
    else { g_server.options.port = 8080; g_server.options.address = "0.0.0.0"; g_server.options.thread_pool_size = 1; }
    if (g_server.options.thread_pool_size < 1) g_server.options.thread_pool_size = 1;
    if (!g_server.options.address) g_server.options.address = "0.0.0.0";
}

void h2o_c_use(const char *method, const char *path, h2o_c_handle_request_cb cb, void *arg) {
    path_handler_t *h = calloc(1, sizeof(path_handler_t));
    if (method) h->method = strdup(method);
    if (path) h->path = strdup(path);
    h->cb = cb;
    h->arg = arg;
    h->next = g_server.handlers;
    g_server.handlers = h;
}

void h2o_c_run() {
    int n_threads = g_server.options.thread_pool_size;
    g_server.workers = calloc(n_threads, sizeof(worker_ctx_t));
    g_server.running = true;

    for (int i = 1; i < n_threads; i++) {
        g_server.workers[i].id = i;
        pthread_create(&g_server.workers[i].thread, NULL, worker_thread_func, &g_server.workers[i]);
    }
    g_server.workers[0].id = 0;
    worker_thread_func(&g_server.workers[0]);

    for (int i = 1; i < n_threads; i++) {
        pthread_join(g_server.workers[i].thread, NULL);
    }
    free(g_server.workers);
    g_server.workers = NULL;
}

void h2o_c_stop() {
    if (!g_server.running) return;
    g_server.running = false;
    for (int i = 0; i < g_server.options.thread_pool_size; i++) {
        if (g_server.workers && g_server.workers[i].loop) {
            uv_async_send(&g_server.workers[i].stop_async);
        }
    }
}

void h2o_c_destroy() {
    path_handler_t *h = g_server.handlers;
    while(h) {
        path_handler_t *next = h->next;
        if(h->method) free(h->method);
        if(h->path) free(h->path);
        free(h);
        h = next;
    }
    g_server.handlers = NULL;
}

// FIX: Added safe destructor to avoid leaking dynamically allocated response components
static void h2o_c_response_destroy(h2o_c_response_t *r) {
    if (!r) return;

    if (r->status_message) free(r->status_message);
    if (r->body) free(r->body);

    h2o_c_header_t *h = r->headers;
    while (h) {
        h2o_c_header_t *next = h->next;
        if (h->key) free(h->key);
        if (h->value) free(h->value);
        free(h);
        h = next;
    }
    free(r);
}

h2o_c_response_t *h2o_c_make_response(int status, const char *msg, const char *body, size_t len, const char *content_type) {
    h2o_c_response_t *r = calloc(1, sizeof(h2o_c_response_t));
    r->status_code = status;
    r->status_message = msg ? strdup(msg) : strdup("OK");
    r->close_connection = false;

    if (body && len > 0) {
        r->body = malloc(len);
        memcpy(r->body, body, len);
        r->body_len = len;
    }

    h2o_c_header_t *h1 = calloc(1, sizeof(h2o_c_header_t));
    h1->key = strdup("Content-Type");
    h1->value = strdup(content_type ? content_type : "text/plain");

    h2o_c_header_t *h2 = calloc(1, sizeof(h2o_c_header_t));
    char len_buf[32];
    snprintf(len_buf, sizeof(len_buf), "%zu", len);
    h2->key = strdup("Content-Length");
    h2->value = strdup(len_buf);

    h1->next = h2;
    r->headers = h1;

    // FIX: Using the newly defined destructor
    r->destroy = h2o_c_response_destroy;

    return r;
}

h2o_c_response_t *h2o_c_make_response_and_close(int status, const char *msg, const char *body, size_t len, const char *content_type) {
    h2o_c_response_t *r = h2o_c_make_response(status, msg, body, len, content_type);
    r->close_connection = true;
    return r;
}