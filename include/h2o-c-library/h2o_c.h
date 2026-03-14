// SPDX-FileCopyrightText: 2025-2026 Andy Curtis <contactandyc@gmail.com>
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
    char *status_message; // e.g. "OK", "Not Found"
    h2o_c_header_t *headers;
    h2o_c_destroy_cb destroy; // Optional cleanup callback
};

/* --- Callbacks --- */

// Synchronous Callback (Blocking the event loop - FAST for your Zero-Copy parser)
// Returns a response object.
typedef h2o_c_response_t *(*h2o_c_handle_request_cb)(
    void *arg,
    const char *method,
    const char *path,
    const char *body,
    size_t body_len
);

/* --- Configuration --- */

typedef struct {
    bool enable_ssl;        // If true, cert/key must be provided
    const char *cert_file;
    const char *key_file;

    bool enable_http2;      // H2O enables H2 by default usually
    int thread_pool_size;   // Number of H2O Event Loops (Threads) to spawn
    unsigned short port;
    const char *address;    // "0.0.0.0"
} h2o_c_options_t;

/* --- API --- */

// Initialize the wrapper (allocates global state)
void h2o_c_init(h2o_c_options_t *options);

// Register a handler (LIFO or FIFO logic inside)
void h2o_c_use(const char *method,
               const char *path,
               h2o_c_handle_request_cb cb,
               void *arg);

// Start the server (Blocks current thread)
void h2o_c_run();

// Stop the server
void h2o_c_destroy();

/* --- Helper to create a response easily --- */
h2o_c_response_t *h2o_c_make_response(int status, const char *msg,
                                      const char *body, size_t len,
                                      const char *content_type);

#ifdef __cplusplus
}
#endif

#endif
