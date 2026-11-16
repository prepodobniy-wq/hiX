// SPDX-License-Identifier: GPL-2.0-only
//! Rust in the hix kernel (ring 0).
//!
//! This crate is compiled as a `staticlib` for i686 and linked into
//! `build/hix.elf`. It is `no_std`, never allocates and exposes a small C ABI
//! (`hix_rust_*`) that `qubix/crypto.c` wraps for system calls and the shell.
//!
//! The first module is ChaCha20-Poly1305, an example of how to add Rust code
//! for the encryption/decryption tests. Keep the ABI in `include/hix/cryptoabi.h`
//! in sync.

#![cfg_attr(not(test), no_std)]
#![deny(unsafe_op_in_unsafe_fn)]

mod aead;
mod chacha20;
mod poly1305;

pub const ABI_VERSION: u32 = 1;
pub const OK: i32 = 0;
pub const ERR_INVAL: i32 = -22; /* -EINVAL */
pub const ERR_AUTH: i32 = aead::ERR_AUTH;
pub const ERR_IO: i32 = -5; /* -EIO */

#[cfg(not(test))]
#[cfg(feature = "kernel")]
extern "C" {
    fn hix_rust_panic(
        msg: *const u8,
        msg_len: usize,
        file: *const u8,
        file_len: usize,
        line: u32,
    ) -> !;
}

#[cfg(not(test))]
#[cfg(not(feature = "kernel"))]
extern "C" {
    fn abort() -> !;
}

#[cfg(not(test))]
#[cfg(feature = "kernel")]
fn panic_abort(info: &core::panic::PanicInfo) -> ! {
    const MSG: &[u8] = b"panic in Rust module";
    let (file, line) = match info.location() {
        Some(loc) => (loc.file().as_bytes(), loc.line()),
        None => (&b"<rust>"[..], 0),
    };
    unsafe { hix_rust_panic(MSG.as_ptr(), MSG.len(), file.as_ptr(), file.len(), line) }
}

#[cfg(not(test))]
#[cfg(not(feature = "kernel"))]
fn panic_abort(_info: &core::panic::PanicInfo) -> ! {
    unsafe { abort() }
}

#[cfg(not(test))]
#[panic_handler]
fn panic(info: &core::panic::PanicInfo) -> ! {
    panic_abort(info)
}

unsafe fn input_slice<'a>(ptr: *const u8, len: u32) -> Option<&'a [u8]> {
    if len == 0 {
        return Some(&[]);
    }
    if ptr.is_null() {
        return None;
    }
    Some(unsafe { core::slice::from_raw_parts(ptr, len as usize) })
}

unsafe fn output_slice<'a>(ptr: *mut u8, len: u32) -> Option<&'a mut [u8]> {
    if len == 0 {
        return Some(&mut []);
    }
    if ptr.is_null() {
        return None;
    }
    Some(unsafe { core::slice::from_raw_parts_mut(ptr, len as usize) })
}

unsafe fn key_ref<'a>(ptr: *const u8) -> Option<&'a [u8; 32]> {
    if ptr.is_null() {
        return None;
    }
    Some(unsafe { &*(ptr as *const [u8; 32]) })
}

unsafe fn nonce_ref<'a>(ptr: *const u8) -> Option<&'a [u8; 12]> {
    if ptr.is_null() {
        return None;
    }
    Some(unsafe { &*(ptr as *const [u8; 12]) })
}

unsafe fn tag_mut<'a>(ptr: *mut u8) -> Option<&'a mut [u8; 16]> {
    if ptr.is_null() {
        return None;
    }
    Some(unsafe { &mut *(ptr as *mut [u8; 16]) })
}

unsafe fn tag_ref<'a>(ptr: *const u8) -> Option<&'a [u8; 16]> {
    if ptr.is_null() {
        return None;
    }
    Some(unsafe { &*(ptr as *const [u8; 16]) })
}

#[no_mangle]
pub extern "C" fn hix_rust_abi_version() -> u32 {
    ABI_VERSION
}

/// Run RFC 8439 self-tests. 0 on success, -EIO on a mismatch.
#[no_mangle]
pub extern "C" fn hix_rust_selftest() -> i32 {
    if !chacha20::selftest() {
        return ERR_IO;
    }
    if !aead::selftest() {
        return ERR_IO;
    }
    OK
}

/// XOR `len` bytes of `data` with the ChaCha20 keystream.
#[no_mangle]
pub unsafe extern "C" fn hix_rust_chacha20_xor(
    key: *const u8,
    nonce: *const u8,
    counter: u32,
    data: *mut u8,
    len: u32,
) -> i32 {
    let (Some(key), Some(nonce), Some(data)) = (
        unsafe { key_ref(key) },
        unsafe { nonce_ref(nonce) },
        unsafe { output_slice(data, len) },
    ) else {
        return ERR_INVAL;
    };

    chacha20::xor(key, counter, nonce, data);
    OK
}

/// Encrypt `len` bytes from `input` to `output`, then write the tag.
#[no_mangle]
pub unsafe extern "C" fn hix_rust_aead_seal(
    key: *const u8,
    nonce: *const u8,
    aad: *const u8,
    aad_len: u32,
    input: *const u8,
    len: u32,
    output: *mut u8,
    tag: *mut u8,
) -> i32 {
    let (Some(key), Some(nonce), Some(aad), Some(output), Some(tag)) = (
        unsafe { key_ref(key) },
        unsafe { nonce_ref(nonce) },
        unsafe { input_slice(aad, aad_len) },
        unsafe { output_slice(output, len) },
        unsafe { tag_mut(tag) },
    ) else {
        return ERR_INVAL;
    };

    if len != 0 && input.is_null() {
        return ERR_INVAL;
    }
    /* memmove also handles input == output */
    if len != 0 {
        unsafe { core::ptr::copy(input, output.as_mut_ptr(), len as usize) };
    }
    aead::seal_in_place(key, nonce, aad, output, tag);
    OK
}

/// Verify the tag and decrypt `len` bytes from `input` to `output`.
#[no_mangle]
pub unsafe extern "C" fn hix_rust_aead_open(
    key: *const u8,
    nonce: *const u8,
    aad: *const u8,
    aad_len: u32,
    input: *const u8,
    len: u32,
    output: *mut u8,
    tag: *const u8,
) -> i32 {
    let (Some(key), Some(nonce), Some(aad), Some(output), Some(tag)) = (
        unsafe { key_ref(key) },
        unsafe { nonce_ref(nonce) },
        unsafe { input_slice(aad, aad_len) },
        unsafe { output_slice(output, len) },
        unsafe { tag_ref(tag) },
    ) else {
        return ERR_INVAL;
    };

    if len != 0 && input.is_null() {
        return ERR_INVAL;
    }
    if len != 0 {
        unsafe { core::ptr::copy(input, output.as_mut_ptr(), len as usize) };
    }
    if aead::open_in_place(key, nonce, aad, output, tag) {
        OK
    } else {
        ERR_AUTH
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn abi_selftest() {
        assert_eq!(hix_rust_selftest(), 0);
    }

    #[test]
    fn ffi_roundtrip() {
        let key = [0x5au8; 32];
        let nonce = [0xa5u8; 12];
        let aad = b"hix";
        let msg = b"hello from ring 0, Rust says hi";
        let mut plain = [0u8; 32];
        let mut cipher = [0u8; 32];
        let mut plain_out = [0u8; 32];
        let mut tag = [0u8; 16];
        let len = msg.len() as u32;

        plain[..msg.len()].copy_from_slice(msg);

        let r = unsafe {
            hix_rust_aead_seal(
                key.as_ptr(), nonce.as_ptr(), aad.as_ptr(), aad.len() as u32,
                plain.as_ptr(), len, cipher.as_mut_ptr(), tag.as_mut_ptr(),
            )
        };
        assert_eq!(r, 0);
        assert_ne!(&cipher[..msg.len()], &plain[..msg.len()]);

        let r = unsafe {
            hix_rust_aead_open(
                key.as_ptr(), nonce.as_ptr(), aad.as_ptr(), aad.len() as u32,
                cipher.as_ptr(), len, plain_out.as_mut_ptr(), tag.as_ptr(),
            )
        };
        assert_eq!(r, 0);
        assert_eq!(&plain_out[..msg.len()], &plain[..msg.len()]);

        tag[0] ^= 0x80;
        let r = unsafe {
            hix_rust_aead_open(
                key.as_ptr(), nonce.as_ptr(), aad.as_ptr(), aad.len() as u32,
                cipher.as_ptr(), len, plain_out.as_mut_ptr(), tag.as_ptr(),
            )
        };
        assert_eq!(r, ERR_AUTH);
        assert_eq!(plain_out, [0u8; 32]);
    }
}
