(module
  (import "env" "ngx_wasm_req_get_header" (func $req_get_header (param i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_set_status" (func $set_status (param i32) (result i32)))
  (import "env" "ngx_wasm_resp_write" (func $resp_write (param i32 i32) (result i32)))
  (memory 1)
  (export "memory" (memory 0))
  (data (i32.const 0) "x-wasm-test")
  (data (i32.const 128) "header missing\n")
  (data (i32.const 160) "header read error\n")
  (func (export "on_content") (result i32)
    (local $header_len i32)
    i32.const 0
    i32.const 11
    i32.const 32
    i32.const 64
    call $req_get_header
    local.set $header_len
    i32.const 200
    call $set_status
    drop
    local.get $header_len
    i32.const 0
    i32.lt_s
    if
      local.get $header_len
      i32.const -1
      i32.eq
      if
        i32.const 128
        i32.const 15
        call $resp_write
        drop
      else
        i32.const 160
        i32.const 18
        call $resp_write
        drop
      end
      i32.const 0
      return
    end
    i32.const 32
    local.get $header_len
    i32.add
    i32.const 10
    i32.store8
    i32.const 32
    local.get $header_len
    i32.const 1
    i32.add
    call $resp_write
    drop
    i32.const 0))
