(module
  (import "env" "ngx_wasm_resp_get_body_chunk" (func $resp_get_body_chunk (param i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_get_body_chunk_eof" (func $resp_get_body_chunk_eof (result i32)))
  (import "env" "ngx_wasm_resp_set_body_chunk" (func $resp_set_body_chunk (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "X")

  ;; layout:
  ;; 64: carry_len (0 or 1)
  ;; 65: carry byte
  ;; 256: input chunk
  ;; 512: output chunk
  (func (export "on_body") (result i32)
    (local $len i32)
    (local $eof i32)
    (local $in i32)
    (local $out i32)
    (local $out_len i32)
    (local $carry_len i32)
    (local $ch i32)
    (local $next i32)

    i32.const 256
    i32.const 256
    call $resp_get_body_chunk
    local.set $len

    call $resp_get_body_chunk_eof
    local.set $eof

    i32.const 64
    i32.load8_u
    local.set $carry_len

    i32.const 0
    local.set $in

    i32.const 512
    local.set $out
    i32.const 0
    local.set $out_len

    block $done
      loop $loop
        local.get $carry_len
        i32.const 0
        i32.gt_u
        if
          i32.const 65
          i32.load8_u
          local.set $ch
          i32.const 0
          local.set $carry_len
          i32.const 64
          i32.const 0
          i32.store8
        else
          local.get $in
          local.get $len
          i32.ge_u
          if
            br $done
          end

          i32.const 256
          local.get $in
          i32.add
          i32.load8_u
          local.set $ch

          local.get $in
          i32.const 1
          i32.add
          local.set $in
        end

        local.get $ch
        i32.const 97
        i32.eq
        if
          local.get $in
          local.get $len
          i32.lt_u
          if
            i32.const 256
            local.get $in
            i32.add
            i32.load8_u
            local.set $next

            local.get $next
            i32.const 98
            i32.eq
            if
              local.get $out
              i32.const 0
              i32.load8_u
              i32.store8

              local.get $out
              i32.const 1
              i32.add
              local.set $out

              local.get $out_len
              i32.const 1
              i32.add
              local.set $out_len

              local.get $in
              i32.const 1
              i32.add
              local.set $in

              br $loop
            end
          else
            local.get $eof
            i32.eqz
            if
              i32.const 64
              i32.const 1
              i32.store8
              i32.const 65
              local.get $ch
              i32.store8
              br $done
            end
          end
        end

        local.get $out
        local.get $ch
        i32.store8

        local.get $out
        i32.const 1
        i32.add
        local.set $out

        local.get $out_len
        i32.const 1
        i32.add
        local.set $out_len

        br $loop
      end
    end

    i32.const 512
    local.get $out_len
    call $resp_set_body_chunk
    drop

    i32.const 0))
