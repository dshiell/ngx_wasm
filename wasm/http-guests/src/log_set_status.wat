(module
  (import "env" "ngx_wasm_resp_set_status" (func $resp_set_status (param i32) (result i32)))
  (memory (export "memory") 1)

  (func (export "on_log") (result i32)
    i32.const 599
    call $resp_set_status
    drop

    i32.const 0))
