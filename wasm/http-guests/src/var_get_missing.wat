(module
  (import "env" "ngx_wasm_var_get" (func $var_get (param i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_write" (func $resp_write (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "does_not_exist")
  (data (i32.const 32) "missing")
  (data (i32.const 64) "error")

  (func (export "on_content") (result i32)
    i32.const 0
    i32.const 14
    i32.const 96
    i32.const 32
    call $var_get
    i32.const -1
    i32.eq
    if
      i32.const 32
      i32.const 7
      call $resp_write
      drop
      i32.const 0
      return
    end

    i32.const 64
    i32.const 5
    call $resp_write
    drop

    i32.const 0))
