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
- logs the configured module path and export name
- returns a stub plain-text response

No Wasmtime integration exists yet.

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
