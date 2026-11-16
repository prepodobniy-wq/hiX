// SPDX-License-Identifier: GPL-2.0-only
//! ChaCha20 (RFC 8439), integer arithmetic only.
//!
//! The kernel does not enable CR4.OSFXSR and does not save SSE state on a
//! context switch, so this module never uses floating point or SIMD.

const SIGMA: [u32; 4] = [0x6170_7865, 0x3320_646e, 0x7962_2d32, 0x6b20_6574];

fn le32(bytes: &[u8], at: usize) -> u32 {
    u32::from_le_bytes([
        bytes[at],
        bytes[at + 1],
        bytes[at + 2],
        bytes[at + 3],
    ])
}

#[inline(always)]
fn quarter_round(s: &mut [u32; 16], a: usize, b: usize, c: usize, d: usize) {
    s[a] = s[a].wrapping_add(s[b]);
    s[d] ^= s[a];
    s[d] = s[d].rotate_left(16);

    s[c] = s[c].wrapping_add(s[d]);
    s[b] ^= s[c];
    s[b] = s[b].rotate_left(12);

    s[a] = s[a].wrapping_add(s[b]);
    s[d] ^= s[a];
    s[d] = s[d].rotate_left(8);

    s[c] = s[c].wrapping_add(s[d]);
    s[b] ^= s[c];
    s[b] = s[b].rotate_left(7);
}

/// One 64-byte ChaCha20 block at `counter`.
pub(crate) fn block(key: &[u8; 32], counter: u32, nonce: &[u8; 12]) -> [u8; 64] {
    let mut state = [0u32; 16];

    state[0..4].copy_from_slice(&SIGMA);
    for i in 0..8 {
        state[4 + i] = le32(key, i * 4);
    }
    state[12] = counter;
    for i in 0..3 {
        state[13 + i] = le32(nonce, i * 4);
    }

    let initial = state;
    for _ in 0..10 {
        /* columns */
        quarter_round(&mut state, 0, 4, 8, 12);
        quarter_round(&mut state, 1, 5, 9, 13);
        quarter_round(&mut state, 2, 6, 10, 14);
        quarter_round(&mut state, 3, 7, 11, 15);
        /* diagonals */
        quarter_round(&mut state, 0, 5, 10, 15);
        quarter_round(&mut state, 1, 6, 11, 12);
        quarter_round(&mut state, 2, 7, 8, 13);
        quarter_round(&mut state, 3, 4, 9, 14);
    }

    let mut out = [0u8; 64];
    for i in 0..16 {
        let word = state[i].wrapping_add(initial[i]);
        out[i * 4..i * 4 + 4].copy_from_slice(&word.to_le_bytes());
    }
    out
}

/// XOR `data` with the ChaCha20 keystream starting at `counter`.
pub(crate) fn xor(key: &[u8; 32], counter: u32, nonce: &[u8; 12], data: &mut [u8]) {
    let mut counter = counter;
    let mut off = 0;

    while off < data.len() {
        let stream = block(key, counter, nonce);
        let n = core::cmp::min(64, data.len() - off);
        for i in 0..n {
            data[off + i] ^= stream[i];
        }
        off += n;
        counter = counter.wrapping_add(1);
    }
}

pub(crate) fn selftest() -> bool {
    const KEY: [u8; 32] = [
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
        0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    ];
    const NONCE: [u8; 12] = [
        0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x4a, 0x00, 0x00, 0x00, 0x00,
    ];
    const EXPECTED: [u8; 64] = [
        0x10, 0xf1, 0xe7, 0xe4, 0xd1, 0x3b, 0x59, 0x15, 0x50, 0x0f, 0xdd, 0x1f,
        0xa3, 0x20, 0x71, 0xc4, 0xc7, 0xd1, 0xf4, 0xc7, 0x33, 0xc0, 0x68, 0x03,
        0x04, 0x22, 0xaa, 0x9a, 0xc3, 0xd4, 0x6c, 0x4e, 0xd2, 0x82, 0x64, 0x46,
        0x07, 0x9f, 0xaa, 0x09, 0x14, 0xc2, 0xd7, 0x05, 0xd9, 0x8b, 0x02, 0xa2,
        0xb5, 0x12, 0x9c, 0xd1, 0xde, 0x16, 0x4e, 0xb9, 0xcb, 0xd0, 0x83, 0xe8,
        0xa2, 0x50, 0x3c, 0x4e,
    ];

    block(&KEY, 1, &NONCE) == EXPECTED
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rfc8439_block() {
        assert!(selftest());
    }

    #[test]
    fn stream_is_reversible() {
        let key = [0x42u8; 32];
        let nonce = [0x17u8; 12];
        let mut data = [0u8; 257];
        for (i, b) in data.iter_mut().enumerate() {
            *b = i as u8;
        }
        let original = data;
        xor(&key, 7, &nonce, &mut data);
        assert_ne!(data, original);
        xor(&key, 7, &nonce, &mut data);
        assert_eq!(data, original);
    }
}
