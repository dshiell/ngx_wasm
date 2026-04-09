(module
  (import "env" "ngx_wasm_yield" (func $yield (result i32)))
  (import "env" "ngx_wasm_resp_set_status" (func $set_status (param i32) (result i32)))
  (import "env" "ngx_wasm_resp_write" (func $resp_write (param i32 i32) (result i32)))
  (memory 1)
  (export "memory" (memory 0))
  (global $stage (mut i32) (i32.const 0))
  (data (i32.const 0) "hello after two yields\n")
  (func (export "on_content") (result i32)
    global.get $stage
    i32.const 2
    i32.eq
    if
      i32.const 200
      call $set_status
      drop
      i32.const 0
      i32.const 23
      call $resp_write
      drop
      i32.const 0
      return
    end

    global.get $stage
    i32.const 1
    i32.add
    global.set $stage

    call $yield
    drop
    i32.const 0))
