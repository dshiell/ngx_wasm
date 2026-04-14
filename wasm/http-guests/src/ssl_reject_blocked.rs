#![no_std]

use core::panic::PanicInfo;

const BLOCKED_NAME: &[u8] = b"blocked.local";
const SSL_AD_UNRECOGNIZED_NAME: i32 = 112;

extern "C" {
    fn ngx_wasm_ssl_get_server_name(buf_ptr: *mut u8, buf_len: i32) -> i32;
    fn ngx_wasm_ssl_reject_handshake(alert: i32) -> i32;
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    core::arch::wasm32::unreachable()
}

#[no_mangle]
pub extern "C" fn on_ssl_client_hello() -> i32 {
    let mut buf = [0u8; 256];
    let len = unsafe { ngx_wasm_ssl_get_server_name(buf.as_mut_ptr(), buf.len() as i32) };

    if len < 0 {
        return 0;
    }

    let len = len as usize;
    let name = &buf[..core::cmp::min(len, buf.len())];

    if name == BLOCKED_NAME {
        return unsafe { ngx_wasm_ssl_reject_handshake(SSL_AD_UNRECOGNIZED_NAME) };
    }

    0
}
