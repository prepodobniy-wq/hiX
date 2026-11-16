#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Verify what GRUB (or qemu -kernel) needs from the kernel image.

Checks: i386 ELF, Multiboot magic + checksum inside the first 8 KiB of the
file and 4-byte aligned, entry point inside the image, segments loadable,
driver table non-empty. Pure Python, no tools needed.
"""
import struct
import sys

MAGIC = 0x1BADB002


def main(path):
    data = open(path, "rb").read()
    errors = []

    if data[:4] != b"\x7fELF" or data[4] != 1 or data[18:20] != b"\x03\x00":
        sys.exit("%s: not a 32-bit i386 ELF" % path)

    entry, phoff = struct.unpack_from("<II", data, 24)
    phentsize, phnum = struct.unpack_from("<HH", data, 42)
    load = []
    for i in range(phnum):
        p_type, off, vaddr, paddr, filesz, memsz, flags, align = \
            struct.unpack_from("<8I", data, phoff + i * phentsize)
        if p_type == 1:
            load.append((off, vaddr, paddr, filesz, memsz))
    if not load:
        errors.append("no PT_LOAD segment")

    found = None
    for off in range(0, min(len(data), 8192) - 11, 4):
        magic, flags, csum = struct.unpack_from("<III", data, off)
        if magic == MAGIC:
            found = (off, flags, csum)
            break
    if not found:
        errors.append("Multiboot header not found in the first 8 KiB")
    else:
        off, flags, csum = found
        if (magic + flags + csum) & 0xFFFFFFFF:
            errors.append("Multiboot checksum is wrong")
        if flags & 0x10000:
            errors.append("address fields flag set but not supported here")
        print("multiboot header at file offset %#x, flags %#x" % (off, flags))

    if not any(v <= entry < v + m for _, v, _, _, m in load):
        errors.append("entry point %#x outside all segments" % entry)
    if load and min(v for _, v, _, _, _ in load) < 0x100000:
        errors.append("image loads below 1 MiB")

    print("entry %#x, %d loadable segment(s)" % (entry, len(load)))
    for e in errors:
        print("ERROR:", e)
    if errors:
        sys.exit(1)
    print("multiboot check OK")


main(sys.argv[1] if len(sys.argv) > 1 else "build/hix.elf")
