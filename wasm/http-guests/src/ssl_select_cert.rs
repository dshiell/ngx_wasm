#![no_std]

use core::panic::PanicInfo;

const CERT_PEM: &[u8] = include_bytes!("ssl/selected.local.crt");
const KEY_PEM: &[u8] = include_bytes!("ssl/selected.local.key");
const MATCH_NAME: &[u8] = b"selected.local";

extern "C" {
    fn ngx_wasm_ssl_get_server_name(buf_ptr: *mut u8, buf_len: i32) -> i32;
    fn ngx_wasm_ssl_set_certificate(
        cert_ptr: *const u8,
        cert_len: i32,
        key_ptr: *const u8,
        key_len: i32,
    ) -> i32;
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    core::arch::wasm32::unreachable()
}

#[no_mangle]
pub extern "C" fn on_ssl_certificate() -> i32 {
    let mut buf = [0u8; 256];
    let len = unsafe { ngx_wasm_ssl_get_server_name(buf.as_mut_ptr(), buf.len() as i32) };

    if len < 0 {
        return 0;
    }

    let len = len as usize;
    let name = &buf[..core::cmp::min(len, buf.len())];

    if name == MATCH_NAME {
        return unsafe {
            ngx_wasm_ssl_set_certificate(
                CERT_PEM.as_ptr(),
                CERT_PEM.len() as i32,
                KEY_PEM.as_ptr(),
                KEY_PEM.len() as i32,
            )
        };
    }

    0
}
