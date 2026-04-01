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
- routes execution into a Wasmtime C runtime embedding

The initial Wasmtime integration is now present, but still narrow:

- one exported guest function
- one request-local invocation
- per-process in-memory module cache inside the runtime library
- fuel limit set per invocation
- no async/yield path yet

## Implementation Principles

`ngx_wasm` follows a strict KISS discipline.

- keep one obvious lifecycle path for each feature
- avoid fallback behavior that hides missing initialization or broken state
- implement only what is needed now, not speculative future abstractions
- keep functions small and focused on a single job
- keep structs limited to fields with a clear current purpose
- prefer explicit control flow over cleverness
- add complexity only when a real feature requires it and tests justify it

This module will become complex on its own because of NGINX phase semantics,
Wasm runtime integration, gas accounting, and suspension/resume behavior. The
implementation should not add extra incidental complexity on top of that.

## Wasmtime Dependency

`ngx_wasm` uses Wasmtime's C API and expects a pinned release unpacked at:

```sh
third_party/wasmtime-36.0.3
```

Fetch the correct archive for the current macOS/Linux platform and CPU with:

```sh
make wasmtime-fetch
```

The fetch script:

- detects `Darwin` vs `Linux`
- detects `aarch64`/`arm64` vs `x86_64`
- downloads the matching Wasmtime C API release artifact
- extracts it into `third_party/wasmtime-<version>/`
- removes the downloaded tarball after extraction

You can pin a different release by overriding `WASMTIME_VERSION`:

```sh
make WASMTIME_VERSION=36.0.3 wasmtime-fetch
```

If you already have a Wasmtime C API install elsewhere, point the nginx module
build at it with `WASMTIME_DIR`:

```sh
WASMTIME_DIR=/path/to/wasmtime-c-api auto/configure --add-module=../ngx_wasm
```

## Phase 1 ABI shape

The initial ABI is intentionally narrow:

- guest export: `on_content() -> i32`
- host imports planned for the guest:
  - `ngx_wasm_log(level, ptr, len)`
  - `ngx_wasm_resp_set_status(status)`
  - `ngx_wasm_resp_write(ptr, len)`

The host-side ABI implementation exists in:

- [`include/ngx_http_wasm_abi.h`](/Users/derek/projects/nginx-playground/ngx_wasm/include/ngx_http_wasm_abi.h)
- [`src/ngx_http_wasm_abi.c`](/Users/derek/projects/nginx-playground/ngx_wasm/src/ngx_http_wasm_abi.c)
- [`include/ngx_http_wasm_runtime.h`](/Users/derek/projects/nginx-playground/ngx_wasm/include/ngx_http_wasm_runtime.h)
- [`src/ngx_http_wasm_runtime.c`](/Users/derek/projects/nginx-playground/ngx_wasm/src/ngx_http_wasm_runtime.c)

Current copy policy:

- log import may borrow guest memory for the duration of the immediate host call
- response body import copies guest memory into the NGINX request pool

That response-body copy is intentional for correctness. Guest linear memory is
owned by the Wasmtime store and cannot safely outlive the invocation or be used
as an NGINX output buffer without stronger lifetime guarantees.

## Example guest Wasm module

A minimal guest example lives in
[`examples/hello-world`](/Users/derek/projects/nginx-playground/ngx_wasm/examples/hello-world).

Build it with:

```sh
make wasm
```

If your default compiler does not support the `wasm32-unknown-unknown` target,
override `CC` with a wasm-capable clang:

```sh
make CC=/path/to/clang wasm
```

On macOS, the common fix is:

```sh
brew install llvm
make CC=$(brew --prefix llvm)/bin/clang wasm
```

The example build now uses `-fuse-ld=lld` for the final wasm link. If the
compile step works but the link step still fails, your LLVM install is missing
`lld`/`wasm-ld` or it is not visible to the selected `clang`.

On systems where `clang` cannot find the wasm linker automatically, install
Homebrew `lld` and pass it explicitly:

```sh
brew install lld
make CC=$(brew --prefix llvm)/bin/clang WASM_LD=$(brew --prefix lld)/bin/wasm-ld wasm
```

If the `lld` package exposes `ld.lld` instead of `wasm-ld`, use that path
instead.

or directly in the example directory:

```sh
cd examples/hello-world
make build
```

The exported guest entrypoint is `on_content`.

## Build with local nginx source

From [`nginx`](/Users/derek/projects/nginx-playground/nginx):

```sh
cd ../ngx_wasm
make wasmtime-fetch
cd ../nginx
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
            content_by_wasm examples/hello-world/build/hello_world.wasm on_content;
        }
    }
}
```

## Expected current behavior

Requests to `/wasm` should:

- hit the `ngx_wasm` content handler
- log the resolved module path and export name
- execute guest `on_content`
- return the body written by the guest via the ABI
