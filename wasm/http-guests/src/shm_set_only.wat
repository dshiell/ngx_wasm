(module
  (import "env" "ngx_wasm_shm_set" (func $shm_set (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "shared-key")
  (data (i32.const 64) "persisted-value")

  (func (export "on_content") (result i32)
    i32.const 0
    i32.const 10
    i32.const 64
    i32.const 15
    call $shm_set
    drop
    i32.const 0))
