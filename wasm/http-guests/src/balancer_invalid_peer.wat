(module
  (import "env" "ngx_wasm_balancer_set_peer"
    (func $balancer_set_peer (param i32) (result i32)))
  (memory (export "memory") 1)

  (func (export "on_balancer") (result i32)
    (drop (call $balancer_set_peer (i32.const 99)))
    (i32.const 0)))
