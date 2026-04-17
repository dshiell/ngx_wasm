(module
  (import "env" "ngx_wasm_var_set" (func $var_set (param i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_write" (func $resp_write (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "host")
  (data (i32.const 32) "mutated.example")
  (data (i32.const 64) "error")
  (data (i32.const 96) "ok")

  (func (export "on_content") (result i32)
    i32.const 0
    i32.const 4
    i32.const 32
    i32.const 15
    call $var_set
    i32.const -2
    i32.eq
    if
      i32.const 64
      i32.const 5
      call $resp_write
      drop
      i32.const 0
      return
    end

    i32.const 96
    i32.const 2
    call $resp_write
    drop

    i32.const 0))
