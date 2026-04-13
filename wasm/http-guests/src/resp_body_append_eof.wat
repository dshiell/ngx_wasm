(module
  (import "env" "ngx_wasm_resp_get_body_chunk" (func $resp_get_body_chunk (param i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_get_body_chunk_eof" (func $resp_get_body_chunk_eof (result i32)))
  (import "env" "ngx_wasm_resp_set_body_chunk" (func $resp_set_body_chunk (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "-eof")

  (func (export "on_body") (result i32)
    (local $len i32)
    (local $eof i32)

    i32.const 64
    i32.const 256
    call $resp_get_body_chunk
    local.set $len

    call $resp_get_body_chunk_eof
    local.set $eof

    local.get $eof
    i32.const 0
    i32.eq
    if
      i32.const 0
      return
    end

    i32.const 64
    local.get $len
    i32.add
    i32.const 0
    i32.load
    i32.store

    i32.const 64
    local.get $len
    i32.const 4
    i32.add
    call $resp_set_body_chunk
    drop

    i32.const 0))
