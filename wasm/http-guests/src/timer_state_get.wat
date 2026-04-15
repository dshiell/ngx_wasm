(module
  (import "env" "ngx_wasm_shm_get" (func $shm_get (param i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_write" (func $resp_write (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "timer-state")
  (data (i32.const 32) "missing")

  (func (export "on_content") (result i32)
    (local $rc i32)

    i32.const 0
    i32.const 11
    i32.const 64
    i32.const 16
    call $shm_get
    local.set $rc

    local.get $rc
    i32.const 0
    i32.ge_s
    if
      i32.const 64
      local.get $rc
      call $resp_write
      drop
      i32.const 0
      return
    end

    i32.const 32
    i32.const 7
    call $resp_write
    drop

    i32.const 0))
