(module
  (import "env" "ngx_wasm_metric_gauge_set" (func $gauge_set (param i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_metric_gauge_add" (func $gauge_add (param i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_write" (func $resp_write (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "in_flight")
  (data (i32.const 32) "ok")

  (func (export "on_content") (result i32)
    i32.const 0
    i32.const 9
    i32.const 10
    call $gauge_set
    drop

    i32.const 0
    i32.const 9
    i32.const -3
    call $gauge_add
    drop

    i32.const 32
    i32.const 2
    call $resp_write
    drop

    i32.const 0))

