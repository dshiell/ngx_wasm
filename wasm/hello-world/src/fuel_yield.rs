#![no_std]

use core::panic::PanicInfo;
use core::ptr;

const BODY: &[u8] = b"hello after rust fuel yield\n";
static mut SINK: u32 = 0;

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
    let mut n: u32 = 50_000;

    while n != 0 {
        unsafe {
            let value = ptr::read_volatile(core::ptr::addr_of!(SINK));
            ptr::write_volatile(core::ptr::addr_of_mut!(SINK), value ^ n);
        }
        n -= 1;
    }

    unsafe {
        ngx_wasm_resp_set_status(200);
        ngx_wasm_resp_write(BODY.as_ptr(), BODY.len() as i32);
    }

    0
}
