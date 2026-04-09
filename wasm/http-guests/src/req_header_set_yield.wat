(module
  (import "env" "ngx_wasm_req_set_header" (func $req_set_header (param i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_yield" (func $yield (result i32)))
  (import "env" "ngx_wasm_resp_set_status" (func $set_status (param i32) (result i32)))
  (import "env" "ngx_wasm_resp_write" (func $resp_write (param i32 i32) (result i32)))
  (memory 1)
  (export "memory" (memory 0))
  (global $stage (mut i32) (i32.const 0))
  (data (i32.const 0) "x-wasm-test")
  (data (i32.const 32) "set-after-yield")
  (data (i32.const 64) "header set after yield\n")
  (func (export "on_content") (result i32)
    global.get $stage
    if
      i32.const 0
      i32.const 11
      i32.const 32
      i32.const 15
      call $req_set_header
      drop
      i32.const 200
      call $set_status
      drop
      i32.const 64
      i32.const 23
      call $resp_write
      drop
      i32.const 0
      return
    end
    i32.const 1
    global.set $stage
    call $yield
    drop
    i32.const 0))
