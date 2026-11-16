#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Build the SDK programs and pack them into the initfs.

usage: mkapps.py BUILD_DIR [CC]

Every user/apps/NAME.c becomes BUILD_DIR/apps/NAME.elf (32-bit, linked at
0x40000000 together with user/sdk/crt0.c and libhix.c). The initfs is the
archive the kernel unpacks into /bin at boot:

    "HXFS", u32 count, then count * { char name[24]; u32 offset; u32 size },
    then the images (offsets from the start of the archive).
"""
import glob
import os
import struct
import subprocess
import sys

CFLAGS = ["-m32", "-std=gnu11", "-Os", "-Wall", "-Wextra", "-Werror",
          "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
          "-fno-pie", "-no-pie", "-fcf-protection=none",
          "-fno-asynchronous-unwind-tables", "-fno-tree-loop-distribute-patterns",
          "-mno-sse", "-mno-mmx", "-mno-sse2", "-mno-80387", "-mno-red-zone"]
LDFLAGS = ["-nostdlib", "-static", "-Wl,-N", "-Wl,-Ttext=0x40000000",
           "-Wl,--build-id=none", "-Wl,-e,_start", "-Wl,-z,noexecstack"]


def build(root, out, cc):
    sdk = [os.path.join(root, "user/sdk/crt0.c"),
           os.path.join(root, "user/sdk/libhix.c")]
    os.makedirs(os.path.join(out, "apps"), exist_ok=True)
    images = []
    for src in sorted(glob.glob(os.path.join(root, "user/apps/*.c"))):
        name = os.path.splitext(os.path.basename(src))[0]
        elf = os.path.join(out, "apps", name + ".elf")
        subprocess.check_call([cc] + CFLAGS + LDFLAGS + ["-o", elf, src] + sdk)
        with open(elf, "rb") as f:
            images.append((name, f.read()))
    return images


def pack(images):
    if any(len(n) >= 24 for n, _ in images):
        raise SystemExit("mkapps: program name too long")
    head = 8 + 32 * len(images)
    table, blob, off = b"", b"", head
    for name, data in images:
        table += struct.pack("<24sII", name.encode(), off, len(data))
        blob += data
        off += len(data)
    return b"HXFS" + struct.pack("<I", len(images)) + table + blob


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    out = sys.argv[1]
    cc = sys.argv[2] if len(sys.argv) > 2 else "gcc"
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    data = pack(build(root, out, cc))
    with open(os.path.join(out, "initfs.bin"), "wb") as f:
        f.write(data)
    print("initfs: %d bytes" % len(data))


if __name__ == "__main__":
    main()
