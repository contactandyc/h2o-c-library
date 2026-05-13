# h2o-c-library

`h2o-c-library` is a lightweight, embedded HTTP server framework for C. It acts as a streamlined wrapper around the highly efficient **H2O** HTTP server library and **libuv**, providing an accessible, Express.js-style routing API without sacrificing the underlying performance of event-driven asynchronous I/O.

## Architecture & Features (Why use it)

Native H2O and libuv are powerful but require significant boilerplate to safely configure contexts, bind sockets, and manage event loops across multiple threads. This library abstracts that complexity into a developer-friendly interface.

* **Event-Driven Multi-Threading:** The library spawns a configurable pool of worker threads. Each thread runs its own isolated `libuv` event loop and H2O context. Incoming TCP connections are distributed across these threads at the kernel level using `SO_REUSEPORT` / `SO_REUSEADDR`, eliminating thread-synchronization bottlenecks.
* **Declarative Routing:** Define endpoints by HTTP method and URL path. Requests are automatically routed to your custom C callbacks, which receive fully parsed headers, paths, and payloads.
* **Managed Memory Lifecycle:** The library safely duplicates incoming request data (headers, paths) so your callback can process them sequentially. Furthermore, it takes ownership of your outgoing `h2o_c_response_t` objects, automatically freeing memory after the response is transmitted over the wire.
* **Keep-Alive Control:** By default, H2O heavily utilizes HTTP Keep-Alive for performance. The library provides explicit controls (`h2o_c_make_response_and_close`) to force the server to sever the TCP connection immediately after replying, which is critical for shedding load or implementing specific network protocols.
* **Graceful Teardown:** Stopping asynchronous C servers is notoriously difficult. The library implements a multi-phase teardown sequence that stops listeners, flushes H2O's internal timer queues, safely disposes of contexts, and drains the libuv loop to prevent memory leaks and dangling sockets.

## Usage (How to use it)

The primary interface is exposed through `h2o_c.h`.

### 1. Defining a Route Handler

A route handler is a standard C function that receives the request details and returns an `h2o_c_response_t` pointer.

```c
#include "h2o-c-library/h2o_c.h"
#include <string.h>

h2o_c_response_t *handle_hello(void *arg, const char *method, const char *path, 
                               h2o_c_header_t *in_headers, const char *body, size_t body_len) {
    
    const char *reply = "{\"message\": \"Hello, World!\"}";
    size_t len = strlen(reply);

    // Creates a response with a 200 OK status and an application/json content type.
    // The library will automatically free this response object after sending it.
    return h2o_c_make_response(200, "OK", reply, len, "application/json");
}

```

### 2. Initialization and Configuration

Configure the server before running it. You can define the listening port, IP address, and the number of worker threads to spawn.

```c
int main() {
    h2o_c_options_t options = {0};
    options.port = 8080;
    options.address = "0.0.0.0";
    options.thread_pool_size = 4; // Spawn 4 event loops

    h2o_c_init(&options);

    // Register routes. Passing NULL for method matches ANY method.
    // Passing NULL for path matches ANY path (catch-all).
    h2o_c_use("GET", "/api/hello", handle_hello, NULL);

    // Blocks the main thread and starts the server
    h2o_c_run();

    // Clean up router memory after the server stops
    h2o_c_destroy();

    return 0;
}

```

### 3. Graceful Shutdown

To stop the server (e.g., when catching a `SIGINT` or `SIGTERM` signal), call `h2o_c_stop()`. This function is thread-safe and utilizes a `uv_async_t` mailbox to signal all worker threads to cleanly exit their event loops.

```c
#include <signal.h>

void on_signal(int sig) {
    // Safely notifies all libuv worker loops to stop accepting connections, 
    // finish current requests, and exit.
    h2o_c_stop();
}

// Inside main():
// signal(SIGINT, on_signal);

```

## Internal Module Layout

Because this is a focused wrapper, the implementation is contained within a single pair of files:

* **`h2o_c.h`**: Defines the public routing API, configuration structures, request/response models, and memory-safe response constructors.
* **`h2o_c.c`**: Implements the multi-threaded libuv architecture.
* `worker_thread_func`: The core routine executed by every thread, responsible for setting up the H2O context, binding the socket, and running `uv_run`.
* `on_req`: The H2O callback that intercepts incoming HTTP requests, maps them against registered routes, invokes user code, and translates the returned `h2o_c_response_t` back into native H2O buffers.
* `on_stop_async`: The teardown sequence that safely dismantles the H2O configurations and active libuv handles.
