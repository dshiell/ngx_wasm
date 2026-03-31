# ngx_wasm

`ngx_wasm` is an out-of-tree NGINX HTTP module implementing the first
vertical slice of `content_by_wasm`.

## Current status

The current implementation is a Phase 1 skeleton:

- registers `content_by_wasm <module-path> <export>`
- accepts the directive in `http`, `server`, and `location` contexts
- resolves module paths using NGINX prefix-relative semantics
- merges configuration down to the active location
- installs a content handler for location-scoped `content_by_wasm`
- routes execution through a narrow ABI/runtime layer
- stages response data through a host-side ABI context
- currently uses a runtime stub instead of Wasmtime

No Wasmtime integration exists yet.

## Phase 1 ABI shape

The initial ABI is intentionally narrow:

- guest export: `on_content() -> i32`
- host imports planned for the guest:
  - `ngx_wasm_log(level, ptr, len)`
  - `ngx_wasm_resp_set_status(status)`
  - `ngx_wasm_resp_write(ptr, len)`

The host-side ABI implementation already exists in:

- [`include/ngx_http_wasm_abi.h`](/Users/derek/projects/nginx-playground/ngx_wasm/include/ngx_http_wasm_abi.h)
- [`src/ngx_http_wasm_abi.c`](/Users/derek/projects/nginx-playground/ngx_wasm/src/ngx_http_wasm_abi.c)
- [`include/ngx_http_wasm_runtime.h`](/Users/derek/projects/nginx-playground/ngx_wasm/include/ngx_http_wasm_runtime.h)
- [`src/ngx_http_wasm_runtime.c`](/Users/derek/projects/nginx-playground/ngx_wasm/src/ngx_http_wasm_runtime.c)

The current runtime stub uses borrowed static buffers where possible to keep the
response path close to the intended low-copy design.

## Example guest Wasm module

A minimal guest example lives in
[`examples/hello-world`](/Users/derek/projects/nginx-playground/ngx_wasm/examples/hello-world).

Build it with:

```sh
make hello-world
```

or directly:

```sh
cd examples/hello-world
cargo build --release --target wasm32-unknown-unknown
```

The exported guest entrypoint is `on_content`.

## Build with local nginx source

From [`nginx`](/Users/derek/projects/nginx-playground/nginx):

```sh
auto/configure --add-module=../ngx_wasm
make -j"$(sysctl -n hw.ncpu)"
```

## Formatting

If `clang-format` is available in `PATH`:

```sh
make format
make check-format
```

If it is installed elsewhere:

```sh
make CLANG_FORMAT=/path/to/clang-format format
```

## Example configuration

```nginx
http {
    server {
        listen 8080;

        location /wasm {
            content_by_wasm examples/hello-world/target/wasm32-unknown-unknown/release/ngx_wasm_hello_world.wasm on_content;
        }
    }
}
```

## Expected current behavior

Requests to `/wasm` should:

- hit the `ngx_wasm` content handler
- log the resolved module path and export name
- return `200 OK` with body `ngx_wasm phase 1 stub`
