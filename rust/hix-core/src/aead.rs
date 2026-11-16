// SPDX-License-Identifier: GPL-2.0-only
//! ChaCha20-Poly1305 AEAD (RFC 8439 §2.8).
//!
//! The AEAD keeps a single 64-byte one-time key on the stack and feeds the
//! MAC in pieces; it never allocates and never copies the whole message.

use crate::chacha20;
use crate::poly1305::Poly1305;

pub(crate) const ERR_AUTH: i32 = -74; /* -EBADMSG */

fn zeroize(buf: &mut [u8]) {
    for b in buf.iter_mut() {
        unsafe { core::ptr::write_volatile(b, 0) };
    }
}

fn constant_time_eq(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    let mut diff = 0u8;
    for i in 0..a.len() {
        diff |= a[i] ^ b[i];
    }
    diff == 0
}

fn mac(
    key: &[u8; 32],
    nonce: &[u8; 12],
    aad: &[u8],
    ciphertext: &[u8],
    tag: &mut [u8; 16],
) {
    let mut otk = chacha20::block(key, 0, nonce);
    let mut otk_key = [0u8; 32];
    otk_key.copy_from_slice(&otk[..32]);

    let mut p = Poly1305::new(&otk_key);
    p.update(aad);
    p.pad16();
    p.update(ciphertext);
    p.pad16();

    let mut lengths = [0u8; 16];
    lengths[0..8].copy_from_slice(&(aad.len() as u64).to_le_bytes());
    lengths[8..16].copy_from_slice(&(ciphertext.len() as u64).to_le_bytes());
    p.update(&lengths);
    p.finish(tag);

    zeroize(&mut otk_key);
    zeroize(&mut otk);
}

/// Encrypt `data` in place and write the 16-byte tag.
pub(crate) fn seal_in_place(
    key: &[u8; 32],
    nonce: &[u8; 12],
    aad: &[u8],
    data: &mut [u8],
    tag: &mut [u8; 16],
) {
    chacha20::xor(key, 1, nonce, data);
    mac(key, nonce, aad, data, tag);
}

/// Verify the tag, then decrypt `data` in place. On failure the buffer is
/// cleared and `false` is returned.
pub(crate) fn open_in_place(
    key: &[u8; 32],
    nonce: &[u8; 12],
    aad: &[u8],
    data: &mut [u8],
    tag: &[u8; 16],
) -> bool {
    let mut expected = [0u8; 16];

    mac(key, nonce, aad, data, &mut expected);
    if !constant_time_eq(&expected, tag) {
        zeroize(data);
        return false;
    }
    chacha20::xor(key, 1, nonce, data);
    true
}

pub(crate) fn selftest() -> bool {
    const KEY: [u8; 32] = [
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b,
        0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
        0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
    ];
    const NONCE: [u8; 12] = [
        0x07, 0x00, 0x00, 0x00, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
    ];
    const AAD: [u8; 12] = [
        0x50, 0x51, 0x52, 0x53, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    ];
    const PLAINTEXT: [u8; 114] = *b"Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
    const CIPHERTEXT: [u8; 114] = [
        0xd3, 0x1a, 0x8d, 0x34, 0x64, 0x8e, 0x60, 0xdb, 0x7b, 0x86, 0xaf, 0xbc,
        0x53, 0xef, 0x7e, 0xc2, 0xa4, 0xad, 0xed, 0x51, 0x29, 0x6e, 0x08, 0xfe,
        0xa9, 0xe2, 0xb5, 0xa7, 0x36, 0xee, 0x62, 0xd6, 0x3d, 0xbe, 0xa4, 0x5e,
        0x8c, 0xa9, 0x67, 0x12, 0x82, 0xfa, 0xfb, 0x69, 0xda, 0x92, 0x72, 0x8b,
        0x1a, 0x71, 0xde, 0x0a, 0x9e, 0x06, 0x0b, 0x29, 0x05, 0xd6, 0xa5, 0xb6,
        0x7e, 0xcd, 0x3b, 0x36, 0x92, 0xdd, 0xbd, 0x7f, 0x2d, 0x77, 0x8b, 0x8c,
        0x98, 0x03, 0xae, 0xe3, 0x28, 0x09, 0x1b, 0x58, 0xfa, 0xb3, 0x24, 0xe4,
        0xfa, 0xd6, 0x75, 0x94, 0x55, 0x85, 0x80, 0x8b, 0x48, 0x31, 0xd7, 0xbc,
        0x3f, 0xf4, 0xde, 0xf0, 0x8e, 0x4b, 0x7a, 0x9d, 0xe5, 0x76, 0xd2, 0x65,
        0x86, 0xce, 0xc6, 0x4b, 0x61, 0x16,
    ];
    const TAG: [u8; 16] = [
        0x1a, 0xe1, 0x0b, 0x59, 0x4f, 0x09, 0xe2, 0x6a, 0x7e, 0x90, 0x2e, 0xcb,
        0xd0, 0x60, 0x06, 0x91,
    ];

    let mut data = PLAINTEXT;
    let mut tag = [0u8; 16];

    seal_in_place(&KEY, &NONCE, &AAD, &mut data, &mut tag);
    if data != CIPHERTEXT || tag != TAG {
        return false;
    }

    if !open_in_place(&KEY, &NONCE, &AAD, &mut data, &tag) {
        return false;
    }
    if data != PLAINTEXT {
        return false;
    }

    /* a modified tag must be rejected */
    tag[0] ^= 1;
    let mut tampered = CIPHERTEXT;
    if open_in_place(&KEY, &NONCE, &AAD, &mut tampered, &tag) {
        return false;
    }
    tampered == [0u8; 114]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rfc8439_aead() {
        assert!(selftest());
    }

    #[test]
    fn empty_and_partial_sizes() {
        let key = [0x11u8; 32];
        let nonce = [0x22u8; 12];
        let aad = [0x33u8; 13];

        for len in [0usize, 1, 15, 16, 17, 63, 64, 65, 255] {
            let mut data = [0u8; 255];
            for (i, b) in data.iter_mut().take(len).enumerate() {
                *b = i as u8;
            }
            let original = data;
            let mut tag = [0u8; 16];

            seal_in_place(&key, &nonce, &aad, &mut data[..len], &mut tag);
            assert!(open_in_place(&key, &nonce, &aad, &mut data[..len], &tag));
            assert_eq!(data, original);
        }
    }
}
