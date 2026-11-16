// SPDX-License-Identifier: GPL-2.0-only
//! Poly1305 one-time authenticator (RFC 8439 §2.5).
//!
//! This is the 5 x 26-bit limb version. It uses u32/u64 integer arithmetic
//! only and keeps no large buffers: update() accepts the message in pieces.

const MASK26: u32 = 0x03ff_ffff;

fn le32(bytes: &[u8], at: usize) -> u32 {
    u32::from_le_bytes([
        bytes[at],
        bytes[at + 1],
        bytes[at + 2],
        bytes[at + 3],
    ])
}

pub(crate) struct Poly1305 {
    r: [u32; 5],
    h: [u32; 5],
    pad: [u32; 4],
    buf: [u8; 16],
    leftover: usize,
}

impl Poly1305 {
    pub(crate) fn new(key: &[u8; 32]) -> Self {
        let t0 = le32(key, 0);
        let t1 = le32(key, 4);
        let t2 = le32(key, 8);
        let t3 = le32(key, 12);

        /* r is clamped: the top four bits of each 32-bit word are cleared */
        let r = [
            t0 & MASK26,
            ((t0 >> 26) | (t1 << 6)) & 0x03ff_ff03,
            ((t1 >> 20) | (t2 << 12)) & 0x03ff_c0ff,
            ((t2 >> 14) | (t3 << 18)) & 0x03f0_3fff,
            (t3 >> 8) & 0x000f_ffff,
        ];

        Self {
            r,
            h: [0; 5],
            pad: [le32(key, 16), le32(key, 20), le32(key, 24), le32(key, 28)],
            buf: [0; 16],
            leftover: 0,
        }
    }

    fn blocks(&mut self, m: &[u8], hibit: u32) {
        let r = self.r;
        let s = [
            r[1] * 5,
            r[2] * 5,
            r[3] * 5,
            r[4] * 5,
        ];

        let h0 = self.h[0] + (le32(m, 0) & MASK26);
        let h1 = self.h[1] + ((le32(m, 3) >> 2) & MASK26);
        let h2 = self.h[2] + ((le32(m, 6) >> 4) & MASK26);
        let h3 = self.h[3] + ((le32(m, 9) >> 6) & MASK26);
        let h4 = self.h[4] + ((le32(m, 12) >> 8) | hibit);

        let d0 = h0 as u64 * r[0] as u64
            + h1 as u64 * s[3] as u64
            + h2 as u64 * s[2] as u64
            + h3 as u64 * s[1] as u64
            + h4 as u64 * s[0] as u64;
        let d1 = h0 as u64 * r[1] as u64
            + h1 as u64 * r[0] as u64
            + h2 as u64 * s[3] as u64
            + h3 as u64 * s[2] as u64
            + h4 as u64 * s[1] as u64;
        let d2 = h0 as u64 * r[2] as u64
            + h1 as u64 * r[1] as u64
            + h2 as u64 * r[0] as u64
            + h3 as u64 * s[3] as u64
            + h4 as u64 * s[2] as u64;
        let d3 = h0 as u64 * r[3] as u64
            + h1 as u64 * r[2] as u64
            + h2 as u64 * r[1] as u64
            + h3 as u64 * r[0] as u64
            + h4 as u64 * s[3] as u64;
        let d4 = h0 as u64 * r[4] as u64
            + h1 as u64 * r[3] as u64
            + h2 as u64 * r[2] as u64
            + h3 as u64 * r[1] as u64
            + h4 as u64 * r[0] as u64;

        let mut c = d0 >> 26;
        self.h[0] = d0 as u32 & MASK26;

        let d1 = d1 + c;
        c = d1 >> 26;
        self.h[1] = d1 as u32 & MASK26;

        let d2 = d2 + c;
        c = d2 >> 26;
        self.h[2] = d2 as u32 & MASK26;

        let d3 = d3 + c;
        c = d3 >> 26;
        self.h[3] = d3 as u32 & MASK26;

        let d4 = d4 + c;
        c = d4 >> 26;
        self.h[4] = d4 as u32 & MASK26;

        self.h[0] += c as u32 * 5;
        c = (self.h[0] >> 26) as u64;
        self.h[0] &= MASK26;
        self.h[1] += c as u32;
    }

    /// Add 16 zero bytes: the padding between AEAD fields.
    pub(crate) fn pad16(&mut self) {
        if self.leftover == 0 {
            return;
        }
        let pad = 16 - self.leftover;
        let zeros = [0u8; 16];
        self.update(&zeros[..pad]);
    }

    pub(crate) fn update(&mut self, mut data: &[u8]) {
        if self.leftover != 0 {
            let want = 16 - self.leftover;
            let n = core::cmp::min(want, data.len());
            self.buf[self.leftover..self.leftover + n].copy_from_slice(&data[..n]);
            self.leftover += n;
            data = &data[n..];
            if self.leftover == 16 {
                let block = self.buf;
                self.blocks(&block, 1 << 24);
                self.leftover = 0;
            }
        }

        while data.len() >= 16 {
            self.blocks(&data[..16], 1 << 24);
            data = &data[16..];
        }

        if !data.is_empty() {
            self.buf[..data.len()].copy_from_slice(data);
            self.leftover = data.len();
        }
    }

    pub(crate) fn finish(mut self, out: &mut [u8; 16]) {
        if self.leftover != 0 {
            self.buf[self.leftover] = 1;
            for b in self.buf.iter_mut().skip(self.leftover + 1) {
                *b = 0;
            }
            let block = self.buf;
            self.blocks(&block, 0);
            self.leftover = 0;
        }

        let mut h0 = self.h[0];
        let mut h1 = self.h[1];
        let mut h2 = self.h[2];
        let mut h3 = self.h[3];
        let mut h4 = self.h[4];

        let mut c = h1 >> 26;
        h1 &= MASK26;
        h2 += c;
        c = h2 >> 26;
        h2 &= MASK26;
        h3 += c;
        c = h3 >> 26;
        h3 &= MASK26;
        h4 += c;
        c = h4 >> 26;
        h4 &= MASK26;
        h0 += c * 5;
        c = h0 >> 26;
        h0 &= MASK26;
        h1 += c;

        /* g = h + 5, then select g if h >= 2^130 - 5 */
        let mut g0 = h0 + 5;
        c = g0 >> 26;
        g0 &= MASK26;
        let mut g1 = h1.wrapping_add(c);
        c = g1 >> 26;
        g1 &= MASK26;
        let mut g2 = h2.wrapping_add(c);
        c = g2 >> 26;
        g2 &= MASK26;
        let mut g3 = h3.wrapping_add(c);
        c = g3 >> 26;
        g3 &= MASK26;
        let g4 = h4.wrapping_add(c).wrapping_sub(1 << 26);

        let mask = (g4 >> 31).wrapping_sub(1);
        g0 &= mask;
        g1 &= mask;
        g2 &= mask;
        g3 &= mask;
        let g4 = g4 & mask;

        let mask = !mask;
        h0 = (h0 & mask) | g0;
        h1 = (h1 & mask) | g1;
        h2 = (h2 & mask) | g2;
        h3 = (h3 & mask) | g3;
        h4 = (h4 & mask) | g4;

        let h0 = (h0 | (h1 << 26)) as u64;
        let h1 = ((h1 >> 6) | (h2 << 20)) as u64;
        let h2 = ((h2 >> 12) | (h3 << 14)) as u64;
        let h3 = ((h3 >> 18) | (h4 << 8)) as u64;
        let pad = self.pad;

        let mut f = h0 + pad[0] as u64;
        out[0..4].copy_from_slice(&(f as u32).to_le_bytes());
        f = h1 + pad[1] as u64 + (f >> 32);
        out[4..8].copy_from_slice(&(f as u32).to_le_bytes());
        f = h2 + pad[2] as u64 + (f >> 32);
        out[8..12].copy_from_slice(&(f as u32).to_le_bytes());
        f = h3 + pad[3] as u64 + (f >> 32);
        out[12..16].copy_from_slice(&(f as u32).to_le_bytes());
    }
}
