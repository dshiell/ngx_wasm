(module
  (import "env" "ngx_wasm_resp_set_status" (func $set_status (param i32) (result i32)))
  (import "env" "ngx_wasm_resp_write" (func $resp_write (param i32 i32) (result i32)))
  (memory 1)
  (export "memory" (memory 0))
  (data (i32.const 0) "hello after many fuel yields\n")
  (func (export "on_content") (result i32)
    (local i32)
    i32.const 200000
    local.set 0
    (block $done
      (loop $loop
        local.get 0
        i32.eqz
        br_if $done
        local.get 0
        i32.const 1
        i32.sub
        local.set 0
        br $loop))
    i32.const 200
    call $set_status
    drop
    i32.const 0
    i32.const 29
    call $resp_write
    drop
    i32.const 0)
)
