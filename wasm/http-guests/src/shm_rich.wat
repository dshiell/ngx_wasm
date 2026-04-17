(module
  (import "env" "ngx_wasm_shm_exists" (func $shm_exists (param i32 i32) (result i32)))
  (import "env" "ngx_wasm_shm_incr" (func $shm_incr (param i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_shm_set" (func $shm_set (param i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_shm_add" (func $shm_add (param i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_shm_replace" (func $shm_replace (param i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_shm_get" (func $shm_get (param i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_resp_write" (func $resp_write (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "flag-key")
  (data (i32.const 32) "counter-key")
  (data (i32.const 64) "bad-counter")
  (data (i32.const 96) "first-value")
  (data (i32.const 128) "second-value")
  (data (i32.const 160) "0")
  (data (i32.const 192) "oops")
  (data (i32.const 224) "present")
  (data (i32.const 256) "missing")
  (data (i32.const 288) "added")
  (data (i32.const 320) "exists")
  (data (i32.const 352) "replaced")
  (data (i32.const 384) "error")

  (func $write (param $ptr i32) (param $len i32)
    local.get $ptr
    local.get $len
    call $resp_write
    drop)

  (func (export "on_exists_flag") (result i32)
    i32.const 0
    i32.const 8
    call $shm_exists
    i32.const 1
    i32.eq
    if
      i32.const 224
      i32.const 7
      call $write
    else
      i32.const 256
      i32.const 7
      call $write
    end
    i32.const 0)

  (func (export "on_add_flag") (result i32)
    i32.const 0
    i32.const 8
    i32.const 96
    i32.const 11
    call $shm_add
    i32.const 0
    i32.eq
    if
      i32.const 288
      i32.const 5
      call $write
    else
      i32.const 384
      i32.const 5
      call $write
    end
    i32.const 0)

  (func (export "on_add_flag_again") (result i32)
    i32.const 0
    i32.const 8
    i32.const 128
    i32.const 12
    call $shm_add
    i32.const -1
    i32.eq
    if
      i32.const 320
      i32.const 6
      call $write
    else
      i32.const 384
      i32.const 5
      call $write
    end
    i32.const 0)

  (func (export "on_replace_flag") (result i32)
    i32.const 0
    i32.const 8
    i32.const 128
    i32.const 12
    call $shm_replace
    i32.const 0
    i32.eq
    if
      i32.const 352
      i32.const 8
      call $write
    else
      i32.const 384
      i32.const 5
      call $write
    end
    i32.const 0)

  (func (export "on_replace_missing") (result i32)
    i32.const 64
    i32.const 11
    i32.const 128
    i32.const 12
    call $shm_replace
    i32.const -1
    i32.eq
    if
      i32.const 256
      i32.const 7
      call $write
    else
      i32.const 384
      i32.const 5
      call $write
    end
    i32.const 0)

  (func (export "on_get_flag") (result i32)
    (local $len i32)
    i32.const 0
    i32.const 8
    i32.const 448
    i32.const 32
    call $shm_get
    local.set $len
    local.get $len
    i32.const 0
    i32.lt_s
    if
      i32.const 384
      i32.const 5
      call $write
    else
      i32.const 448
      local.get $len
      call $write
    end
    i32.const 0)

  (func (export "on_seed_counter") (result i32)
    i32.const 32
    i32.const 11
    i32.const 160
    i32.const 1
    call $shm_set
    i32.const 0
    i32.eq
    if
      i32.const 160
      i32.const 1
      call $write
    else
      i32.const 384
      i32.const 5
      call $write
    end
    i32.const 0)

  (func (export "on_incr_counter") (result i32)
    (local $rc i32)
    (local $len i32)
    i32.const 32
    i32.const 11
    i32.const 2
    call $shm_incr
    local.set $rc
    local.get $rc
    i32.const -2
    i32.eq
    if
      i32.const 384
      i32.const 5
      call $write
      i32.const 0
      return
    end

    i32.const 32
    i32.const 11
    i32.const 480
    i32.const 32
    call $shm_get
    local.set $len

    local.get $len
    i32.const 0
    i32.lt_s
    if
      i32.const 384
      i32.const 5
      call $write
    else
      i32.const 480
      local.get $len
      call $write
    end
    i32.const 0)

  (func (export "on_seed_bad_counter") (result i32)
    i32.const 64
    i32.const 11
    i32.const 192
    i32.const 4
    call $shm_set
    i32.const 0
    i32.eq
    if
      i32.const 192
      i32.const 4
      call $write
    else
      i32.const 384
      i32.const 5
      call $write
    end
    i32.const 0)

  (func (export "on_incr_bad_counter") (result i32)
    i32.const 64
    i32.const 11
    i32.const 1
    call $shm_incr
    i32.const -2
    i32.eq
    if
      i32.const 384
      i32.const 5
      call $write
    else
      i32.const 224
      i32.const 7
      call $write
    end
    i32.const 0)
)
