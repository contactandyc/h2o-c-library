// SPDX-FileCopyrightText: 2019–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef _H2O_C_H
#define _H2O_C_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Data Structures --- */

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

    // NEW: Direct command to H2O's internal state machine to kill Keep-Alive
    bool close_connection;
};

/* --- Callbacks --- */

typedef h2o_c_response_t *(*h2o_c_handle_request_cb)(
    void *arg,
    const char *method,
    const char *path,
    h2o_c_header_t *in_headers,
    const char *body,
    size_t body_len
);

/* --- Configuration --- */

typedef struct {
    bool enable_ssl;
    const char *cert_file;
    const char *key_file;

    bool enable_http2;
    int thread_pool_size;
    unsigned short port;
    const char *address;
} h2o_c_options_t;

/* --- API --- */

void h2o_c_init(h2o_c_options_t *options);

void h2o_c_use(const char *method,
               const char *path,
               h2o_c_handle_request_cb cb,
               void *arg);

void h2o_c_run();
void h2o_c_stop();
void h2o_c_destroy();

/* --- Helpers --- */

h2o_c_response_t *h2o_c_make_response(int status, const char *msg,
                                      const char *body, size_t len,
                                      const char *content_type);

// Response that forces H2O to sever the connection immediately after transmitting
h2o_c_response_t *h2o_c_make_response_and_close(int status, const char *msg,
                                                const char *body, size_t len,
                                                const char *content_type);

#ifdef __cplusplus
}
#endif
#endif