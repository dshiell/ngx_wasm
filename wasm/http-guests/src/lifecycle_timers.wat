(module
  (import "env" "ngx_wasm_log" (func $log (param i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_metric_counter_inc" (func $counter_inc (param i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_metric_gauge_set" (func $gauge_set (param i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_timer_set" (func $timer_set (param i32 i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_timer_cancel" (func $timer_cancel (param i32) (result i32)))
  (import "env" "ngx_wasm_req_get_header" (func $req_get_header (param i32 i32 i32 i32) (result i32)))
  (import "env" "ngx_wasm_shm_set" (func $shm_set (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (global $tick_count (mut i32) (i32.const 0))
  (data (i32.const 0) "init hook ran")
  (data (i32.const 32) "exit hook ran")
  (data (i32.const 64) "worker_starts_total")
  (data (i32.const 96) "timer_ticks_total")
  (data (i32.const 128) "timer_state_gauge")
  (data (i32.const 160) "timer_replaced_total")
  (data (i32.const 192) "timer_original_total")
  (data (i32.const 224) "timer-state")
  (data (i32.const 256) "done")
  (data (i32.const 288) "on_tick")
  (data (i32.const 320) "on_original_tick")
  (data (i32.const 352) "on_replaced_tick")
  (data (i32.const 384) "on_forbidden_request")
  (data (i32.const 416) "x-test")
  (data (i32.const 448) "forbidden timer fired")
  (data (i32.const 512) "forbidden timer returned")

  (func (export "on_init") (result i32)
    i32.const 6
    i32.const 0
    i32.const 13
    call $log
    drop
    i32.const 0)

  (func (export "on_init_worker") (result i32)
    i32.const 64
    i32.const 19
    i32.const 1
    call $counter_inc
    drop

    i32.const 1
    i32.const 100
    i32.const 1
    i32.const 288
    i32.const 7
    call $timer_set
    drop

    i32.const 2
    i32.const 40
    i32.const 1
    i32.const 320
    i32.const 16
    call $timer_set
    drop

    i32.const 2
    i32.const 40
    i32.const 0
    i32.const 352
    i32.const 16
    call $timer_set
    drop

    i32.const 3
    i32.const 60
    i32.const 0
    i32.const 384
    i32.const 20
    call $timer_set
    drop

    i32.const 0)

  (func (export "on_tick") (result i32)
    global.get $tick_count
    i32.const 1
    i32.add
    global.set $tick_count

    i32.const 96
    i32.const 17
    i32.const 1
    call $counter_inc
    drop

    i32.const 128
    i32.const 17
    global.get $tick_count
    call $gauge_set
    drop

    global.get $tick_count
    i32.const 3
    i32.eq
    if
      i32.const 224
      i32.const 11
      i32.const 256
      i32.const 4
      call $shm_set
      drop

      i32.const 1
      call $timer_cancel
      drop
    end

    i32.const 0)

  (func (export "on_original_tick") (result i32)
    i32.const 192
    i32.const 20
    i32.const 1
    call $counter_inc
    drop

    i32.const 2
    call $timer_cancel
    drop

    i32.const 0)

  (func (export "on_replaced_tick") (result i32)
    i32.const 160
    i32.const 20
    i32.const 1
    call $counter_inc
    drop

    i32.const 2
    call $timer_cancel
    drop

    i32.const 0)

  (func (export "on_forbidden_request") (result i32)
    i32.const 6
    i32.const 448
    i32.const 21
    call $log
    drop

    i32.const 416
    i32.const 6
    i32.const 480
    i32.const 16
    call $req_get_header
    drop

    i32.const 6
    i32.const 512
    i32.const 24
    call $log
    drop

    i32.const 0)

  (func (export "on_exit_worker") (result i32)
    i32.const 6
    i32.const 32
    i32.const 13
    call $log
    drop
    i32.const 0))
