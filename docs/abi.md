# Guest ABI

This document describes the current `ngx_wasm` guest ABI for language SDKs and
hand-written guest modules.

Status: early and intentionally narrow. Expect changes as the module evolves.

## Version

- ABI version: `1`
- host-side definition: [ngx_http_wasm_abi.h](/Users/derek/projects/nginx-playground/ngx_wasm/include/ngx_http_wasm_abi.h)

## Execution Model

Current Phase 1 execution model:

- one guest export is invoked for `content_by_wasm`
- one guest export is invoked for `rewrite_by_wasm`
- one request-local invocation per request
- guest code runs inside a fresh Wasmtime store per invocation
- host state is request-local
- guest must return `0` on success

Current SSL hook execution model:

- one synchronous guest export may be invoked during TLS client-hello handling
- one synchronous guest export may be invoked during TLS certificate selection
- these hooks are connection-scoped, not request-scoped
- they do not support suspension or yielding
- they do not expose normal HTTP request/response mutation APIs

Current lifecycle/timer execution model:

- `init_by_wasm` runs synchronously during nginx module init
- `init_worker_by_wasm` runs synchronously once per worker
- `exit_worker_by_wasm` runs synchronously on worker shutdown, best effort
- `ngx_wasm_timer_set()` and `ngx_wasm_timer_cancel()` are available only from
  worker and timer contexts
- each active timer owns a persistent worker-scoped wasm instance reused across
  timer firings
- timer instances are worker-local and do not share linear memory with requests
  or other workers
- timer callbacks do not support suspension or yielding in v1

## Required Guest Export

Current required guest export:

```c
int on_content(void);
```

Current nginx configuration shape:

```nginx
location /rewrite {
    rewrite_by_wasm /path/to/module.wasm on_content;
}

location /wasm {
    content_by_wasm /path/to/module.wasm on_content;
}
```

## Imported Host Functions

Current planned/imported host functions:

```c
int ngx_wasm_log(int level, const void *ptr, int len);
int ngx_wasm_resp_set_status(int status);
int ngx_wasm_shm_get(const void *key_ptr, int key_len,
                     void *buf_ptr, int buf_len);
int ngx_wasm_shm_set(const void *key_ptr, int key_len,
                     const void *value_ptr, int value_len);
int ngx_wasm_shm_delete(const void *key_ptr, int key_len);
int ngx_wasm_metric_counter_inc(const void *name_ptr, int name_len, int delta);
int ngx_wasm_metric_gauge_set(const void *name_ptr, int name_len, int value);
int ngx_wasm_metric_gauge_add(const void *name_ptr, int name_len, int delta);
int ngx_wasm_timer_set(int timer_id, int delay_ms, int repeat,
                       const void *export_name_ptr, int export_name_len);
int ngx_wasm_timer_cancel(int timer_id);
int ngx_wasm_req_set_header(const void *name_ptr, int name_len,
                            const void *value_ptr, int value_len);
int ngx_wasm_req_get_header(const void *name_ptr, int name_len,
                            void *buf_ptr, int buf_len);
int ngx_wasm_ssl_get_server_name(void *buf_ptr, int buf_len);
int ngx_wasm_ssl_reject_handshake(int alert);
int ngx_wasm_ssl_set_certificate(const void *cert_ptr, int cert_len,
                                 const void *key_ptr, int key_len);
int ngx_wasm_resp_write(const void *ptr, int len);
```

Expected semantics:

- `ngx_wasm_log`
  - logs a guest-provided byte slice at the requested nginx log level
  - log levels must map to nginx-supported levels
  - messages longer than `1024` bytes are truncated
  - guest bytes are never treated as a trusted format string
  - returns `0` on success

- `ngx_wasm_resp_set_status`
  - sets the HTTP response status code
  - returns `0` on success

- `ngx_wasm_shm_get`
  - reads a byte-string value from the configured `wasm_shm_zone`
  - copies up to `buf_len` bytes into `buf_ptr`
  - returns the full stored value length on success, even if truncated by the
    provided buffer
  - returns `-1` when the key is missing
  - returns `-2` on invalid arguments, missing zone, or host-side failure

- `ngx_wasm_shm_set`
  - inserts or replaces a byte-string value in the configured `wasm_shm_zone`
  - keys and values are compared and stored byte-for-byte
  - returns `0` on success
  - returns `-2` on invalid arguments, missing zone, or allocation failure

- `ngx_wasm_shm_delete`
  - removes a key from the configured `wasm_shm_zone`
  - returns `0` on success, including deleting a missing key
  - returns `-2` on invalid arguments, missing zone, or host-side failure

- `ngx_wasm_metric_counter_inc`
  - increments a declared counter in the configured `wasm_metrics_zone`
  - metric names must be declared with `wasm_counter`
  - delta must be non-negative
  - returns `0` on success
  - returns `-2` for unknown metrics, invalid arguments, missing zone, or type
    mismatch

- `ngx_wasm_metric_gauge_set`
  - sets a declared gauge in the configured `wasm_metrics_zone`
  - metric names must be declared with `wasm_gauge`
  - returns `0` on success
  - returns `-2` for unknown metrics, invalid arguments, missing zone, or type
    mismatch

- `ngx_wasm_metric_gauge_add`
  - adds a signed delta to a declared gauge in the configured
    `wasm_metrics_zone`
  - metric names must be declared with `wasm_gauge`
  - returns `0` on success
  - returns `-2` for unknown metrics, invalid arguments, missing zone, or type
    mismatch

- `ngx_wasm_timer_set`
  - schedules or replaces a worker-local timer by numeric `timer_id`
  - `delay_ms` is the timer delay in milliseconds
  - `repeat != 0` creates a recurring timer, otherwise one-shot
  - `export_name_ptr` and `export_name_len` select the guest export to invoke
    when the timer fires
  - the timer callback runs in its own persistent worker-scoped instance
  - returns `0` on success
  - returns `-2` on invalid arguments, invalid export selection, or host-side
    allocation/setup failure

- `ngx_wasm_timer_cancel`
  - cancels a worker-local timer by numeric `timer_id`
  - canceling a missing timer succeeds
  - returns `0` on success
  - returns `-2` on invalid arguments or host-side failure

- `ngx_wasm_req_set_header`
  - sets or replaces a request header visible to later nginx processing
  - currently intended for request metadata mutation rather than full
    re-parsing of special core headers
  - returns `0` on success

- `ngx_wasm_req_get_header`
  - looks up a request header by case-insensitive name from current request
    state
  - copies up to `buf_len` bytes of the header value into guest memory at
    `buf_ptr`
  - returns the full header value length on success, even if truncated by the
    provided buffer
  - returns `-1` when the header is missing
  - returns `-2` on invalid arguments or host-side failure

- `ngx_wasm_resp_write`
  - writes the response body from guest linear memory
  - returns `0` on success

- `ngx_wasm_ssl_get_server_name`
  - returns the current TLS SNI server name during SSL hooks
  - copies up to `buf_len` bytes into `buf_ptr`
  - returns the full length on success
  - returns `-1` when no SNI value is present

- `ngx_wasm_ssl_reject_handshake`
  - marks the current TLS handshake for rejection
  - the provided alert is currently honored only from the client-hello hook
  - returns `0` on success

- `ngx_wasm_ssl_set_certificate`
  - installs a PEM-encoded certificate and private key on the current TLS
    connection
  - currently supports a single certificate and private key pair
  - does not yet support certificate-chain installation
  - returns `0` on success

## Memory Model

Current assumptions:

- guest strings/buffers are passed as `(ptr, len)` pairs
- pointers refer to guest linear memory
- guest must export a memory named `memory`
- host validates bounds before reading guest memory

Current copy policy:

- log reads may borrow guest memory for the duration of the immediate call
- response body writes are copied into the NGINX request pool
- SSL hook state is connection-local and lasts only for the current handshake
- shared-memory KV state is host-owned and shared across workers through an
  nginx shared memory zone
- metrics state is host-owned and shared across workers through an nginx
  shared memory zone
- worker timers are host-owned nginx event-loop timers and do not survive a full
  restart

## Return Conventions

Current conventions:

- guest export returns `0` on success
- non-zero guest return values are treated as execution failure
- host import functions return `0` on success
- shared-memory operations return `0` on success except `ngx_wasm_shm_get`,
  which returns the stored length on success
- metric operations return `0` on success
- timer operations return `0` on success
- `ngx_wasm_req_get_header` returns a non-negative length on success
- `ngx_wasm_req_get_header` returns `-1` for missing headers
- `ngx_wasm_shm_get` returns `-1` for missing keys
- negative/error returns are otherwise reserved for host-side failures

## Current Shared Memory KV Limitations

- a single default `wasm_shm_zone` is supported in v1
- keys and values are opaque bytes with byte-for-byte comparison
- maximum key length is currently `256` bytes
- maximum value length is currently `65535` bytes
- no TTL, eviction, atomic increment, or list operations yet
- data survives nginx reload while the zone remains configured, but not a full
  nginx restart

## Current Metrics Limitations

- a single default `wasm_metrics_zone` is supported in v1
- metric names must be declared in nginx config with `wasm_counter` or
  `wasm_gauge`
- no labels, histograms, or guest-created metric names
- metrics use signed integer values in v1
- counters saturate at the maximum signed 64-bit value on overflow
- metric data survives nginx reload while the zone remains configured, but not
  a full nginx restart

## Current Lifecycle And Timer Limitations

- `init_by_wasm`, `init_worker_by_wasm`, `exit_worker_by_wasm`, and timer
  callbacks are synchronous only
- `ngx_wasm_yield()` is not allowed in lifecycle or timer contexts
- request header/body APIs, response mutation APIs, and SSL APIs are not
  available in lifecycle or timer contexts
- timer ids are scoped to a single worker
- setting the same timer id replaces the previous timer in that worker
- recurring timers rearm after successful callback completion
- timer callback failure logs an error and cancels the offending timer
- timer instances are persistent across firings, but `init_worker_by_wasm` and
  each timer callback use separate instances in v1

## Current SSL Limitations

- `ssl_client_hello_by_wasm` and `ssl_certificate_by_wasm` are synchronous only
- `ngx_wasm_yield()` is not allowed in SSL hooks
- HTTP request headers, request body, response headers, response status, and
  response body APIs are not available in SSL hooks
- `ssl_certificate_by_wasm` currently supports only in-memory PEM certificate
  and key installation
- certificate chain installation is not implemented yet
- TLS session fetch/store hooks are not implemented yet

## Minimal C Guest Example

```c
#include <ngx_wasm_guest_abi.h>

NGX_WASM_EXPORT("on_content")
int on_content(void) {
    static const char log_message[] = "hello-world guest invoked";
    static const char body[] = "hello from guest wasm\n";

    ngx_wasm_log(NGX_WASM_LOG_NOTICE, log_message, sizeof(log_message) - 1);
    ngx_wasm_resp_set_status(200);
    ngx_wasm_resp_write(body, sizeof(body) - 1);

    return 0;
}
```

## SDK Guidance

Language SDKs should:

- wrap the raw imported functions in typed helpers
- treat this document as the authoritative ABI contract
- version themselves intentionally against ABI changes
- keep generated/hand-written guest code aligned with actual module behavior

When the ABI changes, this document should be updated in the same change.
