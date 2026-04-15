(module
  (import "env" "ngx_wasm_metric_counter_inc" (func $counter_inc (param i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "logged_total")

  (func (export "on_log") (result i32)
    i32.const 0
    i32.const 12
    i32.const 1
    call $counter_inc
    drop

    i32.const 0))
