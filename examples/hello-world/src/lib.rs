const NGX_HTTP_WASM_LOG_NOTICE: u32 = 6;

unsafe extern "C" {
    fn ngx_wasm_log(level: u32, ptr: *const u8, len: usize) -> i32;
    fn ngx_wasm_resp_set_status(status: i32) -> i32;
    fn ngx_wasm_resp_write(ptr: *const u8, len: usize) -> i32;
}

#[unsafe(no_mangle)]
pub extern "C" fn on_content() -> i32 {
    let log_message = b"hello-world guest invoked";
    let body = b"hello from guest wasm\n";

    unsafe {
        let _ = ngx_wasm_log(
            NGX_HTTP_WASM_LOG_NOTICE,
            log_message.as_ptr(),
            log_message.len(),
        );
        let _ = ngx_wasm_resp_set_status(200);
        let _ = ngx_wasm_resp_write(body.as_ptr(), body.len());
    }

    0
}
