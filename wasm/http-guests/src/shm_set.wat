(module
  (import "env" "ngx_wasm_shm_set" (func $shm_set (param i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_write" (func $resp_write (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "shared-key")
  (data (i32.const 64) "persisted-value")
  (data (i32.const 128) "stored")
  (data (i32.const 160) "error")

  (func (export "on_content") (result i32)
    i32.const 0
    i32.const 10
    i32.const 64
    i32.const 15
    call $shm_set
    i32.const 0
    i32.ne
    if
      i32.const 160
      i32.const 5
      call $resp_write
      drop
    else
      i32.const 128
      i32.const 6
      call $resp_write
      drop
    end
    i32.const 0))
