# Design Notes

This document captures implementation-oriented design guidance for `ngx_wasm`.
It is separate from the top-level README on purpose.

For the broader project specification, see
[ngx_wasm_openresty_style_spec.md](/Users/derek/projects/nginx-playground/ngx_wasm_openresty_style_spec.md).

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

## SDK Policy

Guest SDKs should be versioned intentionally alongside the module ABI, but the
core repository should stay focused.

- keep the core repo centered on the nginx module and ABI
- keep one low-level raw guest path for minimal smoke tests and debugging
- allow a small number of first-party SDKs when they materially improve
  end-to-end testing and real-world usage
- add SDKs only when their maintenance cost is justified
- do not add multi-language SDK sprawl by default

The expected staged approach is:

- raw C guest examples for low-level ABI testing
- one first-class SDK, likely Rust, for higher-level end-to-end integration
- additional language SDKs only when there is clear demand and a maintenance
  plan

## Runtime Notes

Current runtime direction:

- Wasmtime C API embedding
- one exported guest function per invocation
- per-worker runtime initialization
- per-process in-memory module cache
- per-invocation fuel limit
- no async/yield path yet

Current copy policy:

- log imports may borrow guest memory for the duration of the immediate host call
- response body imports copy guest memory into the NGINX request pool

That response-body copy is intentional for correctness. Guest linear memory is
owned by the Wasmtime store and cannot safely outlive the invocation or be used
as an NGINX output buffer without stronger lifetime guarantees.
