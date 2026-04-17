(module
  (import "env" "ngx_wasm_time_unix_ms" (func $time_unix_ms (param i32 i32) (result i32)))
  (import "env" "ngx_wasm_time_monotonic_ms" (func $time_monotonic_ms (param i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_write" (func $resp_write (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 64) "ok")
  (data (i32.const 80) "error")

  (func (export "on_content") (result i32)
    i32.const 0
    i32.const 8
    call $time_unix_ms
    i32.const 0
    i32.ne
    if
      i32.const 80
      i32.const 5
      call $resp_write
      drop
      i32.const 0
      return
    end

    i32.const 0
    i64.load
    i64.eqz
    if
      i32.const 80
      i32.const 5
      call $resp_write
      drop
      i32.const 0
      return
    end

    i32.const 16
    i32.const 8
    call $time_monotonic_ms
    i32.const 0
    i32.ne
    if
      i32.const 80
      i32.const 5
      call $resp_write
      drop
      i32.const 0
      return
    end

    i32.const 24
    i32.const 8
    call $time_monotonic_ms
    i32.const 0
    i32.ne
    if
      i32.const 80
      i32.const 5
      call $resp_write
      drop
      i32.const 0
      return
    end

    i32.const 24
    i64.load
    i32.const 16
    i64.load
    i64.lt_u
    if
      i32.const 80
      i32.const 5
      call $resp_write
      drop
      i32.const 0
      return
    end

    i32.const 64
    i32.const 2
    call $resp_write
    drop

    i32.const 0))
