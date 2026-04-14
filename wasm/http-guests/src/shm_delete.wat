(module
  (import "env" "ngx_wasm_shm_delete" (func $shm_delete (param i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_write" (func $resp_write (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "shared-key")
  (data (i32.const 64) "deleted")
  (data (i32.const 96) "error")

  (func (export "on_content") (result i32)
    i32.const 0
    i32.const 10
    call $shm_delete
    i32.const 0
    i32.ne
    if
      i32.const 96
      i32.const 5
      call $resp_write
      drop
    else
      i32.const 64
      i32.const 7
      call $resp_write
      drop
    end
    i32.const 0))
