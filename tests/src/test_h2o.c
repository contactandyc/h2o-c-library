// SPDX-FileCopyrightText: 2019–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include "h2o-c-library/h2o_c.h"
#include "the-macro-library/macro_test.h"

#define TEST_PORT 9998
#define TEST_HOST "127.0.0.1"

/* --- Handlers (Updated for Asynchronous API) --- */

static void handle_ping(h2o_c_req_t *req, void *arg, const char *m, const char *p, h2o_c_header_t *h, const char *b, size_t l) {
    (void)arg; (void)m; (void)p; (void)h; (void)b; (void)l;
    h2o_c_response_t *resp = h2o_c_make_response(200, "OK", "pong", 4, "text/plain");
    h2o_c_send_response(req, resp);
}

static void handle_echo(h2o_c_req_t *req, void *arg, const char *m, const char *p, h2o_c_header_t *h, const char *b, size_t l) {
    (void)arg; (void)m; (void)p; (void)h;
    h2o_c_response_t *resp = h2o_c_make_response(200, "OK", b, l, "application/octet-stream");
    h2o_c_send_response(req, resp);
}

static void handle_json(h2o_c_req_t *req, void *arg, const char *m, const char *p, h2o_c_header_t *h, const char *b, size_t l) {
    (void)arg; (void)m; (void)p; (void)h; (void)b; (void)l;
    h2o_c_response_t *resp = h2o_c_make_response(201, "Created", "{}", 2, "application/json");
    h2o_c_send_response(req, resp);
}

/* --- Client Helper --- */
static char *client_send(const char *raw_request, size_t req_len, int *out_len) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return NULL;

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(TEST_PORT);
    inet_pton(AF_INET, TEST_HOST, &serv_addr.sin_addr);

    // Retry connect for a bit while server starts
    int retries = 50;
    while (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        usleep(20000); // 20ms
        retries--;
        if(retries == 0) { close(sock); return NULL; }
    }

    // Write request
    size_t sent = 0;
    while(sent < req_len) {
        ssize_t n = write(sock, raw_request + sent, req_len - sent);
        if (n <= 0) break;
        sent += n;
    }

    // Read loop (Robust against packet fragmentation)
    char *resp = malloc(16384);
    memset(resp, 0, 16384);
    size_t total_read = 0;

    // Give server time to reply (simple timeout loop)
    for(int i=0; i<50; i++) {
        ssize_t n = read(sock, resp + total_read, 16383 - total_read);
        if (n > 0) {
            total_read += n;
            usleep(10000);
        } else if (n == 0) {
            break; // Connection closed
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(10000);
                continue;
            }
            break; // Error
        }
    }

    close(sock);
    if (total_read == 0) { free(resp); return NULL; }
    if (out_len) *out_len = (int)total_read;
    return resp;
}

/* --- Tests --- */
MACRO_TEST(test_sanity_ping) {
    const char req[] = "GET /test/ping HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    int len = 0;
    char *resp = client_send(req, strlen(req), &len);
    MACRO_ASSERT_TRUE(resp != NULL);

    // 1. Verify Status Line
    if (strstr(resp, "HTTP/1.1 200 OK") == NULL) printf("Response was:\n%s\n", resp);
    MACRO_ASSERT_TRUE(strstr(resp, "HTTP/1.1 200 OK") != NULL);

    // 2. Locate the start of the body (after double CRLF)
    char *body = strstr(resp, "\r\n\r\n");
    MACRO_ASSERT_TRUE(body != NULL);

    // 3. Verify the content "pong" exists in the body
    if (strstr(body, "pong") == NULL) printf("Body was missing 'pong'. Full response:\n%s\n", resp);
    MACRO_ASSERT_TRUE(strstr(body, "pong") != NULL);

    free(resp);
}

MACRO_TEST(test_post_echo) {
    const char req[] = "POST /test/echo HTTP/1.1\r\nContent-Length: 5\r\nHost: localhost\r\nConnection: close\r\n\r\nhello";
    int len = 0;
    char *resp = client_send(req, strlen(req), &len);
    MACRO_ASSERT_TRUE(resp != NULL);

    if (strstr(resp, "hello") == NULL) printf("Response was:\n%s\n", resp);
    MACRO_ASSERT_TRUE(strstr(resp, "hello") != NULL);
    free(resp);
}

MACRO_TEST(test_header_generation) {
    const char req[] = "GET /test/json HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    int len = 0;
    char *resp = client_send(req, strlen(req), &len);
    MACRO_ASSERT_TRUE(resp != NULL);

    MACRO_ASSERT_TRUE(strstr(resp, "HTTP/1.1 201 Created") != NULL);

    if (strstr(resp, "Content-Type: application/json") == NULL) printf("Response was:\n%s\n", resp);
    MACRO_ASSERT_TRUE(strstr(resp, "Content-Type: application/json") != NULL);

    free(resp);
}

MACRO_TEST(test_404_not_found) {
    const char req[] = "GET /garbage/path HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    int len = 0;
    char *resp = client_send(req, strlen(req), &len);
    MACRO_ASSERT_TRUE(resp != NULL);
    MACRO_ASSERT_TRUE(strstr(resp, "HTTP/1.1 404") != NULL);
    free(resp);
}

MACRO_TEST(test_method_routing) {
    const char req[] = "PUT /test/echo HTTP/1.1\r\nContent-Length: 5\r\nHost: localhost\r\nConnection: close\r\n\r\n12345";
    int len = 0;
    char *resp = client_send(req, strlen(req), &len);
    MACRO_ASSERT_TRUE(resp != NULL);
    MACRO_ASSERT_TRUE(strstr(resp, "12345") != NULL);
    free(resp);
}

static void *concurrent_client_thread(void *arg) {
    (void)arg; // Silence the unused parameter warning
    const char req[] = "GET /test/ping HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    for(int i = 0; i < 50; i++) {
        int len = 0;
        char *resp = client_send(req, strlen(req), &len);
        if (resp) {
            if (strstr(resp, "pong") == NULL) exit(1);
            free(resp);
        }
    }
    return NULL;
}

MACRO_TEST(test_concurrency_load) {
    pthread_t clients[10];
    for(int i = 0; i < 10; i++) pthread_create(&clients[i], NULL, concurrent_client_thread, NULL);
    for(int i = 0; i < 10; i++) pthread_join(clients[i], NULL);
    MACRO_ASSERT_TRUE(true); // If we reached here without crashing, we passed
}

MACRO_TEST(test_large_body_echo) {
    size_t body_size = 1024 * 1024; // 1MB body
    char *large_body = malloc(body_size);
    memset(large_body, 'A', body_size);

    char header[256];
    sprintf(header, "POST /test/echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", body_size);

    size_t full_req_len = strlen(header) + body_size;
    char *full_req = malloc(full_req_len);
    memcpy(full_req, header, strlen(header));
    memcpy(full_req + strlen(header), large_body, body_size);

    int out_len = 0;
    char *resp = client_send(full_req, full_req_len, &out_len);

    MACRO_ASSERT_TRUE(resp != NULL);
    char *resp_body = strstr(resp, "\r\n\r\n");
    MACRO_ASSERT_TRUE(resp_body != NULL);
    MACRO_ASSERT_TRUE(memchr(resp_body, 'A', out_len - (resp_body - resp)) != NULL);

    free(large_body);
    free(full_req);
    free(resp);
}

MACRO_TEST(test_keepalive_pipelining) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(TEST_PORT);
    inet_pton(AF_INET, TEST_HOST, &serv_addr.sin_addr);
    connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    const char req[] = "GET /test/ping HTTP/1.1\r\nHost: localhost\r\n\r\n";

    send(sock, req, strlen(req), 0);
    send(sock, req, strlen(req), 0);

    char buffer[8192] = {0};
    int total_received = 0;
    int count = 0;

    for (int i = 0; i < 100; i++) {
        ssize_t n = read(sock, buffer + total_received, sizeof(buffer) - total_received - 1);
        if (n > 0) {
            total_received += n;
            buffer[total_received] = '\0';

            count = 0;
            char *p = buffer;
            while ((p = strstr(p, "HTTP/1.1 200 OK"))) {
                count++;
                p++;
            }
            if (count >= 2) break; // We got both!
        }
        usleep(10000);
    }

    MACRO_ASSERT_EQ_INT(count, 2);
    close(sock);
}


/* --- Main --- */
static void *server_bg_thread(void *arg) {
    h2o_c_server_t *server = (h2o_c_server_t *)arg;
    h2o_c_run(server);
    return NULL;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    h2o_c_options_t opts = {0};
    opts.port = TEST_PORT;
    opts.thread_pool_size = 2;
    opts.address = TEST_HOST;

    // FIX: Initialize the server instance
    h2o_c_server_t *server = h2o_c_init(&opts);

    // FIX: Pass the server pointer to the use functions
    h2o_c_use(server, "GET", "/test/ping", handle_ping, NULL);
    h2o_c_use(server, NULL,  "/test/echo", handle_echo, NULL);
    h2o_c_use(server, "GET", "/test/json", handle_json, NULL);

    pthread_t tid;
    // FIX: Pass the server to the background thread
    pthread_create(&tid, NULL, server_bg_thread, server);

    usleep(200000);

    macro_test_case tests[64];
    size_t test_count = 0;
    MACRO_ADD(tests, test_sanity_ping);
    MACRO_ADD(tests, test_post_echo);
    MACRO_ADD(tests, test_header_generation);
    MACRO_ADD(tests, test_404_not_found);
    MACRO_ADD(tests, test_method_routing);

    MACRO_ADD(tests, test_concurrency_load);
    MACRO_ADD(tests, test_large_body_echo);
    MACRO_ADD(tests, test_keepalive_pipelining);

    macro_run_all("h2o_c_integration", tests, test_count);

    // FIX: Gracefully stop and destroy the instance
    h2o_c_stop(server);
    pthread_join(tid, NULL);
    h2o_c_destroy(server);

    return 0;
}
