#![no_std]

use core::panic::PanicInfo;

const NGX_WASM_ERROR: i32 = -2;
static KEY: [u8; 257] = [b'k'; 257];
static VALUE: &[u8] = b"value";
static SHARED_KEY: &[u8] = b"shared-key";
static OK: &[u8] = b"ok";
static ERROR: &[u8] = b"error";

extern "C" {
    fn ngx_wasm_shm_set(
        key_ptr: *const u8,
        key_len: i32,
        value_ptr: *const u8,
        value_len: i32,
    ) -> i32;
    fn ngx_wasm_resp_write(ptr: *const u8, len: i32) -> i32;
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    core::arch::wasm32::unreachable()
}

fn write_body(body: &[u8]) -> i32 {
    unsafe {
        ngx_wasm_resp_write(body.as_ptr(), body.len() as i32);
    }

    0
}

#[no_mangle]
pub extern "C" fn on_missing_zone() -> i32 {
    let rc = unsafe {
        ngx_wasm_shm_set(
            SHARED_KEY.as_ptr(),
            SHARED_KEY.len() as i32,
            VALUE.as_ptr(),
            VALUE.len() as i32,
        )
    };

    if rc == NGX_WASM_ERROR {
        return write_body(ERROR);
    }

    write_body(OK)
}

#[no_mangle]
pub extern "C" fn on_oversize_key() -> i32 {
    let rc = unsafe {
        ngx_wasm_shm_set(
            KEY.as_ptr(),
            KEY.len() as i32,
            VALUE.as_ptr(),
            VALUE.len() as i32,
        )
    };

    if rc == NGX_WASM_ERROR {
        return write_body(ERROR);
    }

    write_body(OK)
}
