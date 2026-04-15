(module
  (import "env" "ngx_wasm_subreq" (func $subreq (param i32 i32 i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_subreq_get_body_len" (func $subreq_get_body_len (result i32)))
  (import "env" "ngx_wasm_subreq_get_body" (func $subreq_get_body (param i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_write" (func $resp_write (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (global $stage (mut i32) (i32.const 0))
  (data (i32.const 0) "/sub/source")
  (func (export "on_content") (result i32)
    (local $len i32)
    global.get $stage
    if
      call $subreq_get_body_len
      local.set $len
      i32.const 128
      local.get $len
      call $subreq_get_body
      drop
      i32.const 128
      local.get $len
      call $resp_write
      drop
      i32.const 0
      return
    end
    i32.const 1
    global.set $stage
    i32.const 0
    i32.const 11
    i32.const 64
    i32.const 0
    i32.const 0
    i32.const 1
    call $subreq
    drop
    i32.const 0))
