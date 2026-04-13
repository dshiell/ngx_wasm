(module
  (import "env" "ngx_wasm_resp_set_status" (func $resp_set_status (param i32) (result i32)))
  (import "env" "ngx_wasm_resp_set_header" (func $resp_set_header (param i32 i32 i32 i32) (result i32)))
  (memory 1)
  (export "memory" (memory 0))
  (data (i32.const 256) "x-wasm-filter")
  (data (i32.const 320) "status-mutated")
  (func (export "on_header") (result i32)
    i32.const 201
    call $resp_set_status
    drop
    i32.const 256
    i32.const 13
    i32.const 320
    i32.const 14
    call $resp_set_header
    drop
    i32.const 0))
