(module
  (import "env" "ngx_wasm_resp_get_body_chunk" (func $resp_get_body_chunk (param i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_set_body_chunk" (func $resp_set_body_chunk (param i32 i32) (result i32)))
  (memory (export "memory") 1)

  (func (export "on_body") (result i32)
    (local $len i32)
    (local $i i32)
    (local $ch i32)

    i32.const 64
    i32.const 256
    call $resp_get_body_chunk
    local.set $len

    block $done
      local.get $len
      i32.const 0
      i32.le_s
      br_if $done

      i32.const 0
      local.set $i

      block $exit
        loop $loop
          local.get $i
          local.get $len
          i32.ge_u
          br_if $exit

          i32.const 64
          local.get $i
          i32.add
          i32.load8_u
          local.set $ch

          local.get $ch
          i32.const 97
          i32.ge_u
          local.get $ch
          i32.const 122
          i32.le_u
          i32.and
          if
            i32.const 64
            local.get $i
            i32.add
            local.get $ch
            i32.const 32
            i32.sub
            i32.store8
          end

          local.get $i
          i32.const 1
          i32.add
          local.set $i
          br $loop
        end
      end

      i32.const 64
      local.get $len
      call $resp_set_body_chunk
      drop
    end

    i32.const 0))
