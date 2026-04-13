(module
  (import "env" "ngx_wasm_resp_get_header" (func $resp_get_header (param i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_set_header" (func $resp_set_header (param i32 i32 i32 i32) (result i32)))
  (memory 1)
  (export "memory" (memory 0))
  (data (i32.const 256) "x-origin")
  (data (i32.const 320) "x-wasm-observed")
  (data (i32.const 512) "missing")
  (func (export "on_header") (result i32)
    (local $value_len i32)
    i32.const 256
    i32.const 8
    i32.const 0
    i32.const 128
    call $resp_get_header
    local.set $value_len
    local.get $value_len
    i32.const 0
    i32.lt_s
    if
      i32.const 320
      i32.const 15
      i32.const 512
      i32.const 7
      call $resp_set_header
      drop
      i32.const 0
      return
    end
    i32.const 320
    i32.const 15
    i32.const 0
    local.get $value_len
    call $resp_set_header
    drop
    i32.const 0))
