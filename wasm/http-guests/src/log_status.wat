(module
  (import "env" "ngx_wasm_log" (func $log (param i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_get_status" (func $resp_get_status (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "log status=200")
  (data (i32.const 32) "log status=201")
  (data (i32.const 64) "log status=204")
  (data (i32.const 96) "log status=other")

  (func (export "on_log") (result i32)
    (local $status i32)

    call $resp_get_status
    local.set $status

    local.get $status
    i32.const 200
    i32.eq
    if
      i32.const 6
      i32.const 0
      i32.const 14
      call $log
      drop
      i32.const 0
      return
    end

    local.get $status
    i32.const 201
    i32.eq
    if
      i32.const 6
      i32.const 32
      i32.const 14
      call $log
      drop
      i32.const 0
      return
    end

    local.get $status
    i32.const 204
    i32.eq
    if
      i32.const 6
      i32.const 64
      i32.const 14
      call $log
      drop
      i32.const 0
      return
    end

    i32.const 6
    i32.const 96
    i32.const 16
    call $log
    drop

    i32.const 0))
