(module
  (import "env" "ngx_wasm_subreq" (func $subreq (param i32 i32 i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_subreq_get_header" (func $subreq_get_header (param i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_write" (func $resp_write (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (global $stage (mut i32) (i32.const 0))
  (data (i32.const 0) "/sub/header")
  (data (i32.const 32) "x-wasm-filter")
  (func (export "on_content") (result i32)
    (local $len i32)
    global.get $stage
    if
      i32.const 32
      i32.const 13
      i32.const 128
      i32.const 32
      call $subreq_get_header
      local.set $len
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
    i32.const 96
    i32.const 0
    i32.const 0
    i32.const 0
    call $subreq
    drop
    i32.const 0))
