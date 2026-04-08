#![no_std]

use core::panic::PanicInfo;

const NGX_HTTP_WASM_LOG_NOTICE: i32 = 6;
const LOG_MESSAGE: &[u8] = b"health guest invoked";
const BODY: &[u8] = b"ok\n";

extern "C" {
    fn ngx_wasm_log(level: i32, ptr: *const u8, len: i32) -> i32;
    fn ngx_wasm_resp_set_status(status: i32) -> i32;
    fn ngx_wasm_resp_write(ptr: *const u8, len: i32) -> i32;
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    core::arch::wasm32::unreachable()
}

#[no_mangle]
pub extern "C" fn on_content() -> i32 {
    unsafe {
        ngx_wasm_log(
            NGX_HTTP_WASM_LOG_NOTICE,
            LOG_MESSAGE.as_ptr(),
            LOG_MESSAGE.len() as i32,
        );
        ngx_wasm_resp_set_status(200);
        ngx_wasm_resp_write(BODY.as_ptr(), BODY.len() as i32);
    }

    0
}
