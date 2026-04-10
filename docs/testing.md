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
- `BUILD_SANITIZE=1 make build` followed by `make test` should run the same
  suite against an `ASan+UBSan` nginx binary
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
   - server rewrite
   - rewrite
   - access
   - subrequests
   - async fuel/yield behavior
   - reload while requests are suspended

4. `t/005-server-rewrite-basic.t`
   - successful `server_rewrite_by_wasm`
   - inheritance from `http` and `server` scope
   - yield/resume before later phases
   - fallthrough to later phases when no response is produced

Testing principles:

- tests should follow the real nginx module behavior, not test-only shortcuts
- every failure mode should assert both HTTP behavior and the relevant log text
- config-parse failures and request-time failures should be tested separately
- async tests should only be added once the async runtime behavior exists
- sanitizer coverage should run at least on Linux CI, where `ASan+UBSan`
  support is most reliable

## Suspend/Resume Plan

Suspend/resume needs coverage before the full feature is considered stable.
This should be treated as a first-class test matrix, not a few spot checks.

Fuel-based continuation will use Wasmtime async futures, so tests should be
written around poll/reschedule behavior rather than synchronous trap behavior.
The important contract is "future incomplete means suspend and repost", not
"trap means maybe resume".

The minimum staged coverage is:

1. Deterministic reschedule tests
   - manual yield reschedules once and then completes
   - manual yield reschedules multiple times and then completes
   - timeslice exhaustion reschedules and then completes
   - total fuel exhaustion fails instead of rescheduling forever
   - async instantiation can yield and still complete correctly

2. Request-state tests
   - response body is preserved correctly across one or more resumptions
   - fuel remaining is updated correctly across resumptions
   - interruption logs distinguish total-fuel exhaustion from timeslice
     exhaustion

3. Event-loop integration tests
   - resumed requests are reposted fairly and do not spin in a local loop
   - multiple suspended requests can make progress without starving one another

4. Reload and lifecycle tests
   - config reload still works while resumable execution support exists
   - old workers and new workers do not mix config-cycle-owned Wasmtime state
   - suspended requests are cleaned up correctly on worker shutdown

5. Future subrequest and downstream I/O tests
   - buffered subrequest result resumes the parent request on completion
   - streaming subrequest result resumes the parent request as chunks arrive
   - downstream backpressure suspends and resumes body generation correctly
   - client disconnect while suspended cleans up correctly
   - timeout while suspended cleans up correctly

Recommended harness split:

- `Test::Nginx::Socket` for deterministic request-level behavior
- dedicated process/integration scripts for reload, timeout, and future
  streaming cases

Recommended first fixtures for resumable execution:

- one guest that explicitly yields once and then completes
- one guest that yields multiple times before completion
- one guest that runs until timeslice interruption
- later one guest that waits on a host-driven async result

Recommended implementation-order tests for async conversion:

- first prove async instantiation and async call complete without yielding
- then enable fuel async yield interval and prove a single timeslice yield
- then add repeated timeslice yields
- only after that convert manual yield to the same async polling path
