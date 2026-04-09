(module
  (import "env" "ngx_wasm_req_set_header" (func $req_set_header (param i32 i32 i32 i32) (result i32)))
  (memory 1)
  (export "memory" (memory 0))
  (data (i32.const 0) "x-wasm-test")
  (data (i32.const 32) "set-by-guest")
  (func (export "on_content") (result i32)
    i32.const 0
    i32.const 11
    i32.const 32
    i32.const 12
    call $req_set_header
    drop
    i32.const 0))
