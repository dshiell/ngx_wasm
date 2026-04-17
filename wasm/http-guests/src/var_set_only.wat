(module
  (import "env" "ngx_wasm_var_set" (func $var_set (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "wasm_value")
  (data (i32.const 32) "from-wasm")

  (func (export "on_rewrite") (result i32)
    i32.const 0
    i32.const 10
    i32.const 32
    i32.const 9
    call $var_set
    drop
    i32.const 0))
