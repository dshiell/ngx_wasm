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
int ngx_wasm_req_set_header(const void *name_ptr, int name_len,
                            const void *value_ptr, int value_len);
int ngx_wasm_resp_write(const void *ptr, int len);
```

Expected semantics:

- `ngx_wasm_log`
  - logs a guest-provided byte slice at the requested nginx log level
  - returns `0` on success

- `ngx_wasm_resp_set_status`
  - sets the HTTP response status code
  - returns `0` on success

- `ngx_wasm_req_set_header`
  - sets or replaces a request header visible to later nginx processing
  - currently intended for request metadata mutation rather than full
    re-parsing of special core headers
  - returns `0` on success

- `ngx_wasm_resp_write`
  - writes the response body from guest linear memory
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

## Return Conventions

Current conventions:

- guest export returns `0` on success
- non-zero guest return values are treated as execution failure
- host import functions return `0` on success
- negative/error returns are reserved for host-side failures

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
