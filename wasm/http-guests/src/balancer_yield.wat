(module
  (import "env" "ngx_wasm_yield" (func $yield (result i32)))
  (memory (export "memory") 1)

  (func (export "on_balancer") (result i32)
    (drop (call $yield))
    (i32.const 0)))
