// SPDX-FileCopyrightText: 2019–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef _H2O_C_H
#define _H2O_C_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct h2o_c_header_s {
    char *key;
    char *value;
    struct h2o_c_header_s *next;
} h2o_c_header_t;

struct h2o_c_response_s;
typedef struct h2o_c_response_s h2o_c_response_t;
typedef void (*h2o_c_destroy_cb)(h2o_c_response_t *r);

struct h2o_c_response_s {
    char *body;
    size_t body_len;
    int status_code;
    char *status_message;
    h2o_c_header_t *headers;
    h2o_c_destroy_cb destroy;
    bool close_connection;
};

typedef struct h2o_c_req_s h2o_c_req_t;

typedef void (*h2o_c_handle_request_cb)(
    h2o_c_req_t *req,
    void *arg,
    const char *method,
    const char *path,
    h2o_c_header_t *in_headers,
    const char *body,
    size_t body_len
);

typedef struct {
    bool enable_ssl;
    const char *cert_file;
    const char *key_file;
    bool enable_http2;
    int thread_pool_size;
    unsigned short port;
    const char *address;
} h2o_c_options_t;

/* --- THE NEW OBJECT-ORIENTED API --- */

typedef struct h2o_c_server_s h2o_c_server_t;

h2o_c_server_t *h2o_c_init(h2o_c_options_t *options);

void h2o_c_use(h2o_c_server_t *server,
               const char *method,
               const char *path,
               h2o_c_handle_request_cb cb,
               void *arg);

void h2o_c_run(h2o_c_server_t *server);
void h2o_c_stop(h2o_c_server_t *server);
void h2o_c_destroy(h2o_c_server_t *server);

void h2o_c_send_response(h2o_c_req_t *req, h2o_c_response_t *resp);

h2o_c_response_t *h2o_c_make_response(int status, const char *msg, const char *body, size_t len, const char *content_type);
h2o_c_response_t *h2o_c_make_response_and_close(int status, const char *msg, const char *body, size_t len, const char *content_type);

#ifdef __cplusplus
}
#endif
#endif
