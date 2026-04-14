(module
  (import "env" "ngx_wasm_shm_get" (func $shm_get (param i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_write" (func $resp_write (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "shared-key")
  (data (i32.const 128) "not found")

  (func (export "on_content") (result i32)
    (local $len i32)
    i32.const 0
    i32.const 10
    i32.const 192
    i32.const 64
    call $shm_get
    local.set $len

    local.get $len
    i32.const 0
    i32.lt_s
    if
      i32.const 128
      i32.const 9
      call $resp_write
      drop
    else
      i32.const 192
      local.get $len
      call $resp_write
      drop
    end

    i32.const 0))
