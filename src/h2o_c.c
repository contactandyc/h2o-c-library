// SPDX-FileCopyrightText: 2025-2026 Andy Curtis <contactandyc@gmail.com>
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
    h2o_c_options_t options;
    path_handler_t *handlers;
    bool running;
    pthread_t *threads;
} server_ctx_t;

static server_ctx_t g_server;

/* --- H2O Request Handler (The Router) --- */

static int on_req(h2o_handler_t *self, h2o_req_t *req) {
    // 1. Convert H2O types to C strings for the user callback
    // Note: H2O strings are NOT null-terminated, so we must be careful or use len

    // --- Simple Router ---
    path_handler_t *ph = g_server.handlers;
    while (ph) {
        bool method_match = (ph->method == NULL || ph->method[0] == '\0');
        if (!method_match) {
            if (h2o_memis(req->method.base, req->method.len, ph->method, strlen(ph->method))) {
                method_match = true;
            }
        }

        bool path_match = (ph->path == NULL || ph->path[0] == '\0');
        if (!path_match) {
            // Prefix match
            if (req->path.len >= strlen(ph->path) &&
                memcmp(req->path.base, ph->path, strlen(ph->path)) == 0) {
                path_match = true;
            }
        }

        if (method_match && path_match) {
            // FOUND HANDLER

            // Prepare Method/Path as C-Strings for compatibility
            char method_buf[16];
            size_t mlen = req->method.len < 15 ? req->method.len : 15;
            memcpy(method_buf, req->method.base, mlen);
            method_buf[mlen] = '\0';

            // CALL USER
            // Note: req->entity.base might be NULL if content-length is 0
            const char *body_ptr = req->entity.base ? req->entity.base : "";

            h2o_c_response_t *resp = ph->cb(
                ph->arg,
                method_buf,
                req->path_normalized.base, // H2O normalized path
                body_ptr,                  // ZERO-COPY BODY POINTER
                req->entity.len
            );

            if (!resp) {
                // User returned NULL -> treated as "Pass to next"
                ph = ph->next;
                continue;
            }

            // --- SEND RESPONSE ---
            req->res.status = resp->status_code;
            req->res.reason = (resp->status_message) ? resp->status_message : "OK";

            // Headers
            h2o_c_header_t *h = resp->headers;
            while(h) {
                h2o_add_header_by_str(&req->pool, &req->res.headers,
                                      h->key, strlen(h->key), 0, NULL,
                                      h->value, strlen(h->value));
                h = h->next;
            }

            // Body
            h2o_send_inline(req, resp->body, resp->body_len);

            // Cleanup user response
            if (resp->destroy) resp->destroy(resp);
            else free(resp);

            return 0; // Handled
        }
        ph = ph->next;
    }

    return -1; // Not handled
}

/* --- Libuv Accept Shim --- */

static void on_accept(uv_stream_t *listener, int status) {
    if (status != 0) return;

    // 1. Retrieve the H2O accept context (stored in listener->data)
    h2o_accept_ctx_t *accept_ctx = (h2o_accept_ctx_t *)listener->data;

    // 2. Init new Libuv connection
    uv_tcp_t *conn = malloc(sizeof(*conn));
    uv_tcp_init(listener->loop, conn);

    // 3. Accept
    if (uv_accept(listener, (uv_stream_t *)conn) != 0) {
        uv_close((uv_handle_t *)conn, (uv_close_cb)free);
        return;
    }

    // 4. Wrap as H2O socket and hand off to H2O
    h2o_socket_t *sock = h2o_uv_socket_create((uv_stream_t *)conn, (uv_close_cb)free);
    h2o_accept(accept_ctx, sock);
}

/* --- Worker Thread Main Loop --- */

static void *worker_thread_func(void *arg) {
    // 1. Setup Libuv Loop
    uv_loop_t *loop = malloc(sizeof(uv_loop_t));
    uv_loop_init(loop);

    // 2. Setup H2O Context
    h2o_globalconf_t config;
    h2o_hostconf_t *hostconf;
    h2o_pathconf_t *pathconf;
    h2o_context_t ctx;

    // We allocate accept_ctx on the stack of this thread, but it must persist
    // as long as the loop runs. Since loop runs forever, stack is fine.
    h2o_accept_ctx_t accept_ctx;

    h2o_config_init(&config);
    hostconf = h2o_config_register_host(&config, h2o_iovec_init(H2O_STRLIT("default")), 65535);
    pathconf = h2o_config_register_path(hostconf, "/", 0);

    h2o_handler_t *handler = h2o_create_handler(pathconf, sizeof(*handler));
    handler->on_req = on_req;

    h2o_context_init(&ctx, loop, &config);

    if (g_server.options.enable_ssl && g_server.options.cert_file && g_server.options.key_file) {
        // SSL setup would go here using h2o_ssl_register_certificate
    }

    accept_ctx.ctx = &ctx;
    accept_ctx.hosts = config.hosts;
    accept_ctx.ssl_ctx = NULL; // Default no SSL
    accept_ctx.expect_proxy_line = 0;

    // 3. Bind & Listen (SO_REUSEPORT)
    uv_tcp_t listener;
    uv_tcp_init(loop, &listener);

    // Store the accept_ctx in the listener so the callback can find it
    listener.data = &accept_ctx;

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
        free(loop);
        return NULL;
    }

    if (listen(fd, 128) != 0) {
        perror("listen failed");
        free(loop);
        return NULL;
    }

    // Pass the raw FD to libuv
    uv_tcp_open(&listener, fd);

    // Use the Shim Callback 'on_accept' instead of 'h2o_accept'
    if (uv_listen((uv_stream_t *)&listener, 128, on_accept) != 0) {
        fprintf(stderr, "uv_listen failed\n");
        return NULL;
    }

    // Run Loop
    uv_run(loop, UV_RUN_DEFAULT);

    // Cleanup (on exit)
    uv_loop_close(loop);
    free(loop);
    return NULL;
}

/* --- Public API Implementation --- */

void h2o_c_init(h2o_c_options_t *options) {
    memset(&g_server, 0, sizeof(g_server));
    if (options) {
        g_server.options = *options;
    } else {
        // Defaults
        g_server.options.port = 8080;
        g_server.options.address = "0.0.0.0";
        g_server.options.thread_pool_size = 1;
    }

    // Ensure at least one thread
    if (g_server.options.thread_pool_size < 1) {
        g_server.options.thread_pool_size = 1;
    }
    // Default address if null
    if (!g_server.options.address) {
        g_server.options.address = "0.0.0.0";
    }
}

void h2o_c_use(const char *method, const char *path, h2o_c_handle_request_cb cb, void *arg) {
    path_handler_t *h = calloc(1, sizeof(path_handler_t));
    if (method) h->method = strdup(method);
    if (path) h->path = strdup(path);
    h->cb = cb;
    h->arg = arg;

    // Add to head
    h->next = g_server.handlers;
    g_server.handlers = h;
}

void h2o_c_run() {
    int n_threads = g_server.options.thread_pool_size;

    printf("Starting H2O Server on %s:%d with %d threads...\n",
           g_server.options.address, g_server.options.port, n_threads);

    g_server.threads = malloc(sizeof(pthread_t) * n_threads);

    // Spawn N-1 threads
    for (int i = 1; i < n_threads; i++) {
        pthread_create(&g_server.threads[i], NULL, worker_thread_func, NULL);
    }

    // Run the main thread as worker 0
    worker_thread_func(NULL);

    // Join (if main loop exits)
    for (int i = 1; i < n_threads; i++) {
        pthread_join(g_server.threads[i], NULL);
    }
}

void h2o_c_destroy() {
    // Basic cleanup logic...
}

h2o_c_response_t *h2o_c_make_response(int status, const char *msg,
                                      const char *body, size_t len,
                                      const char *content_type) {
    h2o_c_response_t *r = calloc(1, sizeof(h2o_c_response_t));
    r->status_code = status;
    r->status_message = msg ? strdup(msg) : strdup("OK");

    if (body && len > 0) {
        r->body = malloc(len);
        memcpy(r->body, body, len);
        r->body_len = len;
    }

    // Content-Type
    h2o_c_header_t *h1 = calloc(1, sizeof(h2o_c_header_t));
    h1->key = strdup("Content-Type");
    h1->value = strdup(content_type ? content_type : "text/plain");

    // NEW: Content-Length (Prevents Chunked Encoding in tests)
    h2o_c_header_t *h2 = calloc(1, sizeof(h2o_c_header_t));
    char len_buf[32];
    snprintf(len_buf, sizeof(len_buf), "%zu", len);
    h2->key = strdup("Content-Length");
    h2->value = strdup(len_buf);

    h1->next = h2;
    r->headers = h1;

    r->destroy = (h2o_c_destroy_cb)free;
    return r;
}
