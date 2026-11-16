#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Turn an 80x25 cell dump (tests/test_desk.c, DESK_SHOTS=dir) into a PNG.

Usage: screenshot.py in.scr [out.png]

Draws what a VGA text screen shows: the 16 colour palette, code page 437
glyphs, and block characters as exact rectangles instead of font glyphs.
"""
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

COLS, ROWS = 80, 25
CW, CH = 9, 18
SCALE = 1
PALETTE = [
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500,
    0xAAAAAA, 0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF,
    0xFFFF55, 0xFFFFFF,
]
LOW = "\u0000☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼"
FONTS = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/freefont/FreeMono.ttf",
]


def rgb(v):
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


def glyph(code):
    if code < 32:
        return LOW[code]
    if code == 127:
        return "⌂"
    return bytes([code]).decode("cp437")


def main():
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else src.rsplit(".", 1)[0] + ".png"
    data = open(src, "rb").read()
    # cells are 32 bit since 0.3.4; the text look is the low 16 bits
    cells = [c & 0xFFFF for c in struct.unpack("<%dI" % (COLS * ROWS), data[: COLS * ROWS * 4])]
    font = None
    for path in FONTS:
        try:
            font = ImageFont.truetype(path, 15)
            break
        except OSError:
            continue
    img = Image.new("RGB", (COLS * CW, ROWS * CH))
    d = ImageDraw.Draw(img)
    for i, cell in enumerate(cells):
        ch, attr = cell & 255, cell >> 8
        fg, bg = rgb(PALETTE[attr & 15]), rgb(PALETTE[attr >> 4])
        x, y = (i % COLS) * CW, (i // COLS) * CH
        d.rectangle([x, y, x + CW - 1, y + CH - 1], fill=bg)
        if ch == 0xDB:
            d.rectangle([x, y, x + CW - 1, y + CH - 1], fill=fg)
        elif ch == 0xDC:
            d.rectangle([x, y + CH // 2, x + CW - 1, y + CH - 1], fill=fg)
        elif ch == 0xDF:
            d.rectangle([x, y, x + CW - 1, y + CH // 2 - 1], fill=fg)
        elif ch == 0xDD:
            d.rectangle([x, y, x + CW // 2 - 1, y + CH - 1], fill=fg)
        elif ch == 0xDE:
            d.rectangle([x + CW // 2, y, x + CW - 1, y + CH - 1], fill=fg)
        elif ch in (0xB0, 0xB1, 0xB2):
            step = {0xB0: 4, 0xB1: 2, 0xB2: 1}[ch]
            for yy in range(0, CH, 2):
                for xx in range(0, CW, step * 2 if ch != 0xB2 else 1):
                    if ch == 0xB2 and (xx + yy) % 4 == 3:
                        continue
                    d.point((x + xx + (yy // 2 % 2) * step // 2, y + yy), fill=fg)
        elif ch not in (0, 0x20):
            d.text((x + CW // 2, y + CH // 2), glyph(ch), font=font, fill=fg,
                   anchor="mm")
    if SCALE != 1:
        img = img.resize((img.width * SCALE, img.height * SCALE), Image.NEAREST)
    img.save(dst)
    print(dst)


if __name__ == "__main__":
    main()
