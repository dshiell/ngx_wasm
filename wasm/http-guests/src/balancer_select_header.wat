(module
  (import "env" "ngx_wasm_req_get_header"
    (func $req_get_header (param i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_balancer_set_peer"
    (func $balancer_set_peer (param i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "x-route")

  (func (export "on_balancer") (result i32)
    (local $len i32)
    (local.set $len (call $req_get_header
      (i32.const 0)
      (i32.const 7)
      (i32.const 32)
      (i32.const 16)))

    (if
      (i32.and
        (i32.eq (local.get $len) (i32.const 1))
        (i32.eq (i32.load8_u (i32.const 32)) (i32.const 98)))
      (then
        (drop (call $balancer_set_peer (i32.const 1))))
      (else
        (drop (call $balancer_set_peer (i32.const 0)))))

    (i32.const 0)))
