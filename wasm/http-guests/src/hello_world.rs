#![no_std]

use core::panic::PanicInfo;
const BODY: &[u8] = b"hello from guest wasm\n";

extern "C" {
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
        ngx_wasm_resp_set_status(200);
        ngx_wasm_resp_write(BODY.as_ptr(), BODY.len() as i32);
    }

    0
}
