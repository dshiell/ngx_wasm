(module
  (import "env" "ngx_wasm_time_unix_ms" (func $time_unix_ms (param i32 i32) (result i32)))
  (import "env" "ngx_wasm_log" (func $log (param i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 16) "time-ok")

  (func (export "on_log") (result i32)
    i32.const 0
    i32.const 8
    call $time_unix_ms
    i32.const 0
    i32.ne
    if
      i32.const 0
      return
    end

    i32.const 0
    i64.load
    i64.eqz
    if
      i32.const 0
      return
    end

    i32.const 7
    i32.const 16
    i32.const 7
    call $log
    drop

    i32.const 0))
