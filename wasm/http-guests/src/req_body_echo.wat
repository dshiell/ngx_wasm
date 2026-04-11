(module
  (import "env" "ngx_wasm_req_get_body" (func $req_get_body (param i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_set_status" (func $set_status (param i32) (result i32)))
  (import "env" "ngx_wasm_resp_write" (func $resp_write (param i32 i32) (result i32)))
  (memory 1)
  (export "memory" (memory 0))
  (data (i32.const 512) "body unavailable\n")
  (func (export "on_content") (result i32)
    (local $body_len i32)
    i32.const 0
    i32.const 256
    call $req_get_body
    local.set $body_len
    i32.const 200
    call $set_status
    drop
    local.get $body_len
    i32.const 0
    i32.lt_s
    if
      i32.const 512
      i32.const 15
      call $resp_write
      drop
      i32.const 0
      return
    end
    i32.const 0
    local.get $body_len
    call $resp_write
    drop
    i32.const 0))
