# ngx_wasm

`ngx_wasm` is an out-of-tree NGINX HTTP module for running request handlers in
WebAssembly.

The current focus is an OpenResty-style programming model for standard,
unmodified NGINX, starting with `content_by_wasm`.

Project references:

- NGINX: https://github.com/nginx/nginx
- OpenResty / `lua-nginx-module`: https://github.com/openresty/lua-nginx-module
- Wasmtime: https://github.com/bytecodealliance/wasmtime

## What It Does

Today, `ngx_wasm` provides an initial vertical slice of:

- `content_by_wasm <module-path> <export>`
- OpenResty-style configuration placement in `http`, `server`, and `location`
- Wasmtime C API embedding as the current WebAssembly runtime
- one guest export invocation per request
- basic host functions for logging and writing a response

This is still early-stage software, but the direction is clear:

- keep NGINX as the server
- keep the module out-of-tree and usable with stock NGINX
- use WebAssembly for safer, language-flexible guest logic
- learn from OpenResty semantics without requiring OpenResty as the runtime

## Why NGINX

NGINX was chosen because it already provides the core request-processing model
this project wants to extend:

- mature HTTP phase and filter pipeline
- production-proven event-driven architecture
- familiar configuration and inheritance model
- strong operational footprint and ecosystem

The goal is not to replace NGINX. The goal is to make NGINX programmable in a
way that borrows the best ideas from OpenResty while staying compatible with
standard unmodified NGINX.

## Why WebAssembly

WebAssembly was chosen as the guest execution layer because it offers:

- stronger isolation than embedding application logic directly in-process
- a path to multi-language support
- explicit execution budgeting and control
- a better fit for typed SDKs than a dynamic scripting-only model

The current runtime choice is Wasmtime via its C API.

## How It Compares

| Feature | `ngx_wasm` | OpenResty / `lua-nginx-module` | Proxy-Wasm style systems |
| --- | --- | --- | --- |
| Core server model | Standard NGINX module | NGINX/OpenResty | Varies by host proxy |
| Primary inspiration | OpenResty semantics | Native project model | Generalized proxy ABI |
| Guest runtime | WASM via Wasmtime | Lua / LuaJIT | WASM |
| Main goal | Programmable NGINX callbacks | Programmable NGINX callbacks | Portable proxy extensions |
| Config model | NGINX config directives | NGINX config directives | Usually plugin/filter configuration |
| Language model | Multi-language via WASM | Lua-first | Multi-language via WASM |
| Execution budgeting | Planned first-class fuel/gas model | Not the primary model | Often host/runtime dependent |
| Current project state | Early prototype | Mature | Mature in some ecosystems |

OpenResty is the main source of semantic inspiration for this project. The goal
is not to dismiss it, but to carry forward the programmable request-handling
model with a WASM guest runtime.

## Dependencies

Build/runtime dependencies for the current implementation:

- standard NGINX source tree for `--add-module`
- Wasmtime C API
- a C compiler for the nginx module build

Optional example guest build dependencies:

- Rust with the `wasm32-unknown-unknown` target for Rust guest fixtures
- Perl + `prove` for the `Test::Nginx` test suite
- `cpanm` or `cpan` for installing Perl test dependencies via `make deps`

## Performance

Performance numbers are not published yet.

The long-term goal is to publish comparative measurements covering:

- request latency
- host boundary overhead
- guest startup and invocation overhead
- the cost of execution budgeting and future yield/resume behavior

## Quickstart

Fetch the pinned Wasmtime C API release, a local `test-nginx` checkout, the
Rust `stable` toolchain with `wasm32-unknown-unknown`, and Perl test
dependencies:

```sh
make deps
```

Build the example guest:

```sh
make wasm
```

Start a simple local nginx instance for load testing:

```sh
make start
curl http://127.0.0.1:18080/hello
curl http://127.0.0.1:18080/health
make stop
```

Run a repeatable ApacheBench sweep against both wasm endpoints:

```sh
make bench-ab
```

Override the benchmark shape when needed:

```sh
make bench-ab BENCH_AB_REQUESTS=50000 BENCH_AB_CONCURRENCIES="100 500 1000 2000"
```

The benchmark target uses `127.0.0.1:18081` by default so it does not collide
with `make start` on `127.0.0.1:18080`.

`make wasm` builds all current guest modules required by the example and test
suite.

Build NGINX with the module:

```sh
cd ../nginx
auto/configure --add-module=../ngx_wasm
make -j"$(sysctl -n hw.ncpu)"
```

## Basic Usage

Example nginx configuration:

```nginx
http {
    server {
        listen 8080;

        location /wasm {
            content_by_wasm wasm/hello-world/build/hello_world.wasm on_content;
        }
    }
}
```

Current expected behavior:

- the request hits the `ngx_wasm` content handler
- the guest export `on_content` is invoked
- the guest writes the response via the host ABI

## Example Guest Build

The low-level example guest lives in
[`wasm/hello-world`](/Users/derek/projects/nginx-playground/ngx_wasm/wasm/hello-world).

Build it with:

```sh
make wasm
```

`make wasm` builds the Rust guest fixtures with `rustc --target
wasm32-unknown-unknown`.

If needed, override the Rust toolchain explicitly:

```sh
make RUSTC=/path/to/rustc wasm
make RUSTC=/path/to/rustc RUSTUP=/path/to/rustup wasm
```

If the target is missing:

```sh
rustup target add wasm32-unknown-unknown
```

`make deps` also installs `rustup` when needed and ensures the Rust `stable`
toolchain has the `wasm32-unknown-unknown` target available for Rust-based
guest fixtures.

## Documentation

- Project spec: [ngx_wasm_openresty_style_spec.md](/Users/derek/projects/nginx-playground/ngx_wasm_openresty_style_spec.md)
- Design notes: [design.md](/Users/derek/projects/nginx-playground/ngx_wasm/docs/design.md)
- Testing plan: [testing.md](/Users/derek/projects/nginx-playground/ngx_wasm/docs/testing.md)
- Guest ABI: [abi.md](/Users/derek/projects/nginx-playground/ngx_wasm/docs/abi.md)

## Status

This repository is an active prototype. The current code is intentionally
narrow and is being built phase by phase.

## Testing

Install development dependencies and run the current test suite:

```sh
make deps
make test
```

To rebuild nginx with `ASan+UBSan` and run the same suite against the
sanitized binary:

```sh
make build BUILD_SANITIZE=1
make test
```

If your NGINX checkout is not at `../nginx`, override it explicitly:

```sh
make smoke NGINX_DIR=/path/to/nginx
make test NGINX_DIR=/path/to/nginx
make build BUILD_SANITIZE=1 NGINX_DIR=/path/to/nginx
make test NGINX_DIR=/path/to/nginx
```

For debugging, keep the `Test::Nginx` server root and logs on disk:

```sh
TEST_NGINX_NO_CLEAN=1 make test
```

Use a fixed port only when you explicitly want one:

```sh
TEST_NGINX_RANDOMIZE=0 TEST_NGINX_PORT=1984 make test
```

Sanitized builds currently assume a `clang`-compatible toolchain. Override
`SANITIZER_CC`, `SANITIZER_FLAGS`, `ASAN_OPTIONS`, or `UBSAN_OPTIONS` if you
need different sanitizer settings.

The default sanitizer flags disable UBSan's `nonnull-attribute` check and use
an ignorelist for known nginx-core sanitizer findings outside `ngx_wasm`
itself.

On Linux, leak detection stays enabled, but the default `LSAN_OPTIONS` points
at [sanitizers/lsan.supp](/Users/derek/projects/nginx-playground/ngx_wasm/sanitizers/lsan.supp)
to suppress known nginx-core process-lifetime allocations from event startup.
