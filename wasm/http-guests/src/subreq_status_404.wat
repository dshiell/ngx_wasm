(module
  (import "env" "ngx_wasm_subreq" (func $subreq (param i32 i32 i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_subreq_get_status" (func $subreq_get_status (result i32)))
  (import "env" "ngx_wasm_resp_write" (func $resp_write (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (global $stage (mut i32) (i32.const 0))
  (data (i32.const 0) "/sub/missing")
  (data (i32.const 32) "404")
  (data (i32.const 64) "unexpected")
  (func (export "on_content") (result i32)
    global.get $stage
    if
      call $subreq_get_status
      i32.const 404
      i32.eq
      if
        i32.const 32
        i32.const 3
        call $resp_write
        drop
      else
        i32.const 64
        i32.const 10
        call $resp_write
        drop
      end
      i32.const 0
      return
    end
    i32.const 1
    global.set $stage
    i32.const 0
    i32.const 12
    i32.const 96
    i32.const 0
    i32.const 0
    i32.const 0
    call $subreq
    drop
    i32.const 0))
