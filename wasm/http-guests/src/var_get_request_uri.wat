(module
  (import "env" "ngx_wasm_var_get" (func $var_get (param i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_write" (func $resp_write (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "request_uri")

  (func (export "on_content") (result i32)
    (local $value_len i32)
    i32.const 0
    i32.const 11
    i32.const 32
    i32.const 128
    call $var_get
    local.set $value_len

    local.get $value_len
    i32.const 0
    i32.lt_s
    if
      i32.const 0
      return
    end

    i32.const 32
    local.get $value_len
    call $resp_write
    drop

    i32.const 0))
