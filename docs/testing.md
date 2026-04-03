# Testing Plan

`ngx_wasm` uses OpenResty-style testing ideas because the module intentionally
follows OpenResty semantics where practical.

Primary harness direction:

- `Test::Nginx::Socket`

Why:

- it is widely used by OpenResty modules, including `lua-nginx-module`
- it is well suited for nginx module testing with inline config snippets,
  request definitions, response assertions, and error-log matching
- it matches the semantic shape of the behaviors `ngx_wasm` cares about:
  directive parsing, request phases, config inheritance, error paths, and
  eventually async behavior

How OpenResty tests should be used:

- reuse the harness
- reuse test structure and intent where semantics match
- port test ideas selectively instead of copying Lua-specific tests verbatim

Dependency setup:

- `make deps` should fetch the pinned Wasmtime C API and a local
  `test-nginx` checkout under `third_party/`
- `make deps` should install Rust via `rustup` when needed and ensure the
  `wasm32-unknown-unknown` target is available for Rust guest fixtures
- `make test` should be the single entry point for running the current test
  suite
- `NGINX_DIR=/path/to/nginx make test` should be supported so local runs and
  CI jobs do not depend on a hardcoded sibling checkout

Test runtime policy:

- `make test` should use randomized ports by default
- fixed ports should still be supported via `TEST_NGINX_PORT`,
  `TEST_NGINX_SERVER_PORT`, and `TEST_NGINX_CLIENT_PORT`
- the harness should support `TEST_NGINX_NO_CLEAN=1` for debugging
- when `TEST_NGINX_NO_CLEAN=1`, the runner should leave behind a usable
  server root with logs, pid, config, and a small metadata file describing
  the selected port and nginx pid
- `TEST_NGINX_SERVROOT` should be supported when an explicit server root is
  desired for debugging

Current staged plan:

1. `t/001-content-basic.t`
   - successful `content_by_wasm`
   - expected HTTP status
   - expected response body

2. `t/002-content-failures.t`
   - missing export
   - missing memory export
   - guest trap
   - non-zero guest return
   - bad module path

3. Later phase-specific suites
   - rewrite
   - access
   - subrequests
   - async fuel/yield behavior

Testing principles:

- tests should follow the real nginx module behavior, not test-only shortcuts
- every failure mode should assert both HTTP behavior and the relevant log text
- config-parse failures and request-time failures should be tested separately
- async tests should only be added once the async runtime behavior exists
