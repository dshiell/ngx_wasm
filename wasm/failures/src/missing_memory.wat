(module
  (type $resp_set_status_t (func (param i32) (result i32)))
  (type $resp_write_t (func (param i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_set_status"
    (func $ngx_wasm_resp_set_status (type $resp_set_status_t)))
  (import "env" "ngx_wasm_resp_write"
    (func $ngx_wasm_resp_write (type $resp_write_t)))
  (memory 1)
  (func $on_content (export "on_content") (result i32)
    i32.const 200
    call $ngx_wasm_resp_set_status
    drop
    i32.const 0
    i32.const 23
    call $ngx_wasm_resp_write
    drop
    i32.const 0)
  (data (i32.const 0) "missing exported memory\n"))
