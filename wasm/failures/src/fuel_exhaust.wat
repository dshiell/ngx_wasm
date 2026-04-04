(module
  (func (export "on_content") (result i32)
    (local i32)
    i32.const 1000000
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
    i32.const 0)
)
