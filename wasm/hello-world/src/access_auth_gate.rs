#![no_std]

use core::panic::PanicInfo;

const HEADER_NAME: &[u8] = b"x-auth";
const ALLOW_VALUE: &[u8] = b"allow";
const FORBIDDEN_BODY: &[u8] = b"forbidden\n";
const HEADER_BUF_LEN: usize = 16;

extern "C" {
    fn ngx_wasm_req_get_header(
        name_ptr: *const u8,
        name_len: i32,
        buf_ptr: *mut u8,
        buf_len: i32,
    ) -> i32;
    fn ngx_wasm_resp_set_status(status: i32) -> i32;
    fn ngx_wasm_resp_write(ptr: *const u8, len: i32) -> i32;
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    core::arch::wasm32::unreachable()
}

#[no_mangle]
pub extern "C" fn on_content() -> i32 {
    let mut header_buf = [0_u8; HEADER_BUF_LEN];

    let header_len = unsafe {
        ngx_wasm_req_get_header(
            HEADER_NAME.as_ptr(),
            HEADER_NAME.len() as i32,
            header_buf.as_mut_ptr(),
            header_buf.len() as i32,
        )
    };

    if header_len == ALLOW_VALUE.len() as i32
        && &header_buf[..ALLOW_VALUE.len()] == ALLOW_VALUE
    {
        return 0;
    }

    unsafe {
        ngx_wasm_resp_set_status(403);
        ngx_wasm_resp_write(FORBIDDEN_BODY.as_ptr(), FORBIDDEN_BODY.len() as i32);
    }

    0
}
