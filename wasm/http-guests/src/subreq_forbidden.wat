(module
  (import "env" "ngx_wasm_subreq" (func $subreq (param i32 i32 i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (global $stage (mut i32) (i32.const 0))
  (data (i32.const 0) "/sub/source")
  (func (export "on_log") (result i32)
    global.get $stage
    if
      i32.const 0
      return
    end
    i32.const 1
    global.set $stage
    i32.const 0
    i32.const 11
    i32.const 32
    i32.const 0
    i32.const 0
    i32.const 0
    call $subreq
    drop
    i32.const 0))
