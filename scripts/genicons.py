#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Generate the Argent icons (steel/icon_data.c).

Every icon is drawn here with simple shapes at 4x size and scaled down, so
the edges are smooth. The generated file is checked in: building the kernel
needs neither Python nor Pillow. Run `scripts/genicons.py` after changing a
drawing; `scripts/genicons.py --sheet out.png` writes a contact sheet.
"""
import sys
from PIL import Image, ImageDraw, ImageFilter

N = 48                  # icon size in pixels
S = 4                   # supersampling
W = N * S

# the Argent accent colours
INK = (34, 38, 46)
STEEL = (122, 132, 148)
PAPER = (250, 250, 246)
AMBER = (240, 178, 56)
AMBER_D = (204, 138, 28)
TEAL = (46, 150, 142)
BLUE = (58, 108, 190)
RED = (210, 72, 60)
GREEN = (72, 168, 92)
GREEN_D = (36, 110, 62)
PURPLE = (126, 82, 170)


def canvas():
    return Image.new("RGBA", (W, W), (0, 0, 0, 0))


def p(v):
    """a coordinate in icon pixels -> supersampled pixels"""
    return int(round(v * S))


def box(x0, y0, x1, y1):
    return [p(x0), p(y0), p(x1), p(y1)]


def grad(img, bounds, top, bottom, mask_fn):
    """fill the shape drawn by mask_fn(draw) with a vertical gradient"""
    mask = Image.new("L", img.size, 0)
    mask_fn(ImageDraw.Draw(mask))
    x0, y0, x1, y1 = bounds
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    h = max(1, y1 - y0)
    for y in range(y0, y1):
        t = (y - y0) / h
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,)
        d.line([(x0, y), (x1, y)], fill=c)
    img.paste(layer, (0, 0), mask)


def gloss(img, shape_mask_fn, strength=70):
    """a soft white highlight over the top half of a shape"""
    mask = Image.new("L", img.size, 0)
    shape_mask_fn(ImageDraw.Draw(mask))
    hl = Image.new("L", img.size, 0)
    ImageDraw.Draw(hl).rectangle([0, 0, W, int(W * 0.5)], fill=strength)
    both = Image.composite(hl, Image.new("L", img.size, 0), mask)
    white = Image.new("RGBA", img.size, (255, 255, 255, 255))
    img.paste(white, (0, 0), both)


def outline(d, rect, r, color, width=1.2):
    d.rounded_rectangle(rect, radius=p(r), outline=color, width=p(width))


def finish(img):
    """soft drop shadow, then scale down"""
    shadow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    alpha = img.split()[3].point(lambda a: int(a * 0.35))
    shadow.putalpha(alpha)
    shadow = Image.merge("RGBA", (Image.new("L", img.size, 0),) * 3 + (alpha,))
    shadow = shadow.filter(ImageFilter.GaussianBlur(p(1.2)))
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    out.paste(shadow, (p(0.4), p(1.4)))
    out.alpha_composite(img)
    return out.resize((N, N), Image.LANCZOS)


# ---- the icons ---------------------------------------------------------------

def icon_files():
    im = canvas()
    d = ImageDraw.Draw(im)
    d.rounded_rectangle(box(5, 10, 21, 18), p(2.5), fill=AMBER_D)       # tab
    fn = lambda dd: dd.rounded_rectangle(box(5, 13, 43, 38), p(3), fill=255)
    grad(im, (p(5), p(13), p(43), p(38)), (255, 214, 110), AMBER, fn)
    gloss(im, fn, 60)
    outline(d, box(5, 13, 43, 38), 3, AMBER_D)
    d.rounded_rectangle(box(8, 17, 40, 19), p(1), fill=(255, 236, 170))
    return finish(im)


def icon_notes():
    im = canvas()
    d = ImageDraw.Draw(im)
    d.rounded_rectangle(box(9, 5, 37, 42), p(2), fill=PAPER)
    outline(d, box(9, 5, 37, 42), 2, STEEL)
    for i in range(6):
        y = 13 + i * 4.6
        d.line([(p(13), p(y)), (p(33), p(y))], fill=(150, 170, 200), width=p(0.9))
    d.rectangle(box(9, 5, 37, 9.5), fill=RED)
    d.rounded_rectangle(box(9, 5, 37, 9.5), p(2), fill=RED)
    d.rectangle(box(9, 8, 37, 9.5), fill=RED)
    # a pencil across the corner
    pen = [(p(26), p(40)), (p(41), p(25)), (p(44), p(28)), (p(29), p(43))]
    d.polygon(pen, fill=AMBER)
    d.polygon([(p(26), p(40)), (p(29), p(43)), (p(24.5), p(44.5))], fill=(240, 214, 170))
    d.polygon([(p(41), p(25)), (p(44), p(28)), (p(45.4), p(26.6)), (p(42.4), p(23.6))],
              fill=(230, 110, 110))
    return finish(im)


def icon_calc():
    im = canvas()
    d = ImageDraw.Draw(im)
    fn = lambda dd: dd.rounded_rectangle(box(9, 4, 39, 44), p(4), fill=255)
    grad(im, (p(9), p(4), p(39), p(44)), (92, 104, 124), (52, 60, 76), fn)
    gloss(im, fn, 40)
    d.rounded_rectangle(box(12, 8, 36, 17), p(2), fill=(170, 224, 196))
    d.text((p(30), p(8.5)), "", fill=INK)
    for r in range(4):
        for c in range(4):
            x = 12 + c * 6.3
            y = 21 + r * 5.6
            col = AMBER if c == 3 else (232, 236, 242)
            d.rounded_rectangle(box(x, y, x + 5, y + 4.3), p(1.2), fill=col)
    return finish(im)


def icon_clock():
    im = canvas()
    d = ImageDraw.Draw(im)
    d.ellipse(box(5, 5, 43, 43), fill=(214, 218, 226))
    fn = lambda dd: dd.ellipse(box(7, 7, 41, 41), fill=255)
    grad(im, (p(7), p(7), p(41), p(41)), (255, 255, 255), (226, 230, 238), fn)
    d.ellipse(box(7, 7, 41, 41), outline=STEEL, width=p(1))
    import math
    for i in range(12):
        a = math.radians(i * 30)
        r0, r1 = (15.5, 18.5) if i % 3 else (14, 18.5)
        d.line([(p(24 + r0 * math.sin(a)), p(24 - r0 * math.cos(a))),
                (p(24 + r1 * math.sin(a)), p(24 - r1 * math.cos(a)))],
               fill=INK, width=p(1.1 if i % 3 else 1.8))
    d.line([(p(24), p(24)), (p(24), p(11.5))], fill=INK, width=p(2))
    d.line([(p(24), p(24)), (p(33), p(28))], fill=INK, width=p(2))
    d.line([(p(24), p(26.5)), (p(24), p(8.5))], fill=RED, width=p(0.9))
    d.ellipse(box(22.2, 22.2, 25.8, 25.8), fill=RED)
    return finish(im)


def icon_sysinfo():
    im = canvas()
    d = ImageDraw.Draw(im)
    for i in range(5):                                  # pins
        x = 12 + i * 6
        d.rounded_rectangle(box(x, 3, x + 2.4, 45), p(0.8), fill=(190, 196, 206))
    for i in range(5):
        y = 12 + i * 6
        d.rounded_rectangle(box(3, y, 45, y + 2.4), p(0.8), fill=(190, 196, 206))
    fn = lambda dd: dd.rounded_rectangle(box(8, 8, 40, 40), p(4), fill=255)
    grad(im, (p(8), p(8), p(40), p(40)), (74, 92, 124), (40, 52, 78), fn)
    gloss(im, fn, 38)
    d.rounded_rectangle(box(15, 15, 33, 33), p(2), outline=(150, 220, 210), width=p(1.2))
    d.rectangle(box(21, 15, 27, 33), fill=(150, 220, 210))
    d.rectangle(box(15, 21, 33, 27), fill=(150, 220, 210))
    return finish(im)


def icon_tasks():
    im = canvas()
    d = ImageDraw.Draw(im)
    d.rounded_rectangle(box(5, 6, 43, 42), p(3), fill=PAPER)
    outline(d, box(5, 6, 43, 42), 3, STEEL)
    bars = [(11, 36, TEAL), (17, 28, BLUE), (23, 22, GREEN), (29, 14, AMBER), (35, 19, RED)]
    for i, (_, h, col) in enumerate(bars):
        x = 9 + i * 6.4
        d.rounded_rectangle(box(x, 36 - (36 - h) * 0.0 - (42 - h) * 0.55 - 0,
                                x + 4.6, 38.5), p(1), fill=col)
    d.line([(p(8), p(38.5)), (p(40), p(38.5))], fill=INK, width=p(1))
    return finish(im)


def icon_term():
    im = canvas()
    d = ImageDraw.Draw(im)
    fn = lambda dd: dd.rounded_rectangle(box(4, 7, 44, 41), p(4), fill=255)
    grad(im, (p(4), p(7), p(44), p(41)), (52, 58, 70), (24, 28, 36), fn)
    gloss(im, fn, 30)
    outline(d, box(4, 7, 44, 41), 4, (110, 120, 136))
    d.rectangle(box(5, 7, 43, 13), fill=(92, 100, 116))
    d.rounded_rectangle(box(4, 7, 44, 13), p(4), fill=(92, 100, 116))
    d.rectangle(box(4, 10, 44, 13), fill=(92, 100, 116))
    for i, col in enumerate([RED, AMBER, GREEN]):
        d.ellipse(box(7 + i * 4, 8.6, 9.8 + i * 4, 11.4), fill=col)
    d.line([(p(10), p(19)), (p(16), p(24)), (p(10), p(29))], fill=(120, 230, 160),
           width=p(2.2), joint="curve")
    d.rounded_rectangle(box(19, 30, 29, 32.4), p(1), fill=(120, 230, 160))
    return finish(im)


def icon_chess():
    im = canvas()
    d = ImageDraw.Draw(im)
    for r in range(2):
        for c in range(2):
            col = (238, 238, 210) if (r + c) % 2 == 0 else (118, 150, 86)
            d.rectangle(box(4 + c * 20, 6 + r * 18, 24 + c * 20, 24 + r * 18), fill=col)
    d.rounded_rectangle(box(4, 6, 44, 42), p(3), outline=(60, 80, 44), width=p(1.2))
    # a pawn
    fn = lambda dd: (dd.ellipse(box(17.5, 9, 30.5, 22), fill=255),
                     dd.polygon([(p(21), p(20)), (p(27), p(20)), (p(31), p(34)),
                                 (p(17), p(34))], fill=255),
                     dd.rounded_rectangle(box(14, 34, 34, 39.5), p(2), fill=255))
    grad(im, (p(14), p(9), p(34), p(40)), (252, 252, 252), (200, 204, 212), fn)
    mask = Image.new("L", im.size, 0)
    fn(ImageDraw.Draw(mask))
    edge = mask.filter(ImageFilter.MaxFilter(p(1.3) | 1))
    ring = Image.new("RGBA", im.size, (26, 28, 34, 255))
    ring.putalpha(Image.composite(Image.new("L", im.size, 0), edge, mask))
    im.alpha_composite(ring)
    return finish(im)


def icon_mines():
    import math
    im = canvas()
    d = ImageDraw.Draw(im)
    fn = lambda dd: dd.rounded_rectangle(box(4, 4, 44, 44), p(4), fill=255)
    grad(im, (p(4), p(4), p(44), p(44)), (214, 218, 226), (170, 176, 188), fn)
    outline(d, box(4, 4, 44, 44), 4, (110, 118, 132))
    cx = cy = 24
    for i in range(8):
        a = math.radians(i * 45)
        d.line([(p(cx), p(cy)), (p(cx + 13 * math.cos(a)), p(cy + 13 * math.sin(a)))],
               fill=INK, width=p(2.4))
    d.ellipse(box(14, 14, 34, 34), fill=INK)
    d.ellipse(box(18, 17, 22, 21), fill=(230, 236, 246))
    d.ellipse(box(33, 5, 43, 15), fill=RED)
    return finish(im)


def icon_reversi():
    im = canvas()
    d = ImageDraw.Draw(im)
    fn = lambda dd: dd.rounded_rectangle(box(4, 4, 44, 44), p(4), fill=255)
    grad(im, (p(4), p(4), p(44), p(44)), (60, 150, 90), (34, 108, 64), fn)
    for i in range(1, 4):
        d.line([(p(4 + i * 10), p(5)), (p(4 + i * 10), p(43))], fill=(24, 84, 48), width=p(0.8))
        d.line([(p(5), p(4 + i * 10)), (p(43), p(4 + i * 10))], fill=(24, 84, 48), width=p(0.8))
    for (cx, cy, lite) in [(19, 19, True), (29, 19, False), (19, 29, False), (29, 29, True)]:
        d.ellipse(box(cx - 4.4, cy - 3.6, cx + 4.4, cy + 5.2), fill=(20, 40, 28))
        top, bot = ((250, 250, 250), (196, 200, 208)) if lite else ((84, 88, 96), (26, 28, 34))
        m = lambda dd, cx=cx, cy=cy: dd.ellipse(box(cx - 4.4, cy - 4.4, cx + 4.4, cy + 4.4), fill=255)
        grad(im, (p(cx - 5), p(cy - 5), p(cx + 5), p(cy + 5)), top, bot, m)
    return finish(im)


def icon_snake():
    im = canvas()
    d = ImageDraw.Draw(im)
    d.rounded_rectangle(box(4, 4, 44, 44), p(4), fill=(30, 40, 38))
    pts = [(12, 36), (12, 30), (24, 30), (36, 30), (36, 20), (24, 20), (14, 20), (14, 12)]
    for w, col in [(8.6, GREEN_D), (6.6, GREEN)]:
        d.line([(p(x), p(y)) for x, y in pts], fill=col, width=p(w), joint="curve")
        for x, y in (pts[0], pts[-1]):
            d.ellipse(box(x - w / 2, y - w / 2, x + w / 2, y + w / 2), fill=col)
    d.ellipse(box(11, 9, 13.4, 11.4), fill=(255, 255, 255))
    d.ellipse(box(14.8, 9, 17.2, 11.4), fill=(255, 255, 255))
    d.rectangle(box(12.8, 5, 14.2, 8.5), fill=RED)
    d.ellipse(box(30, 33.5, 35, 38.5), fill=RED)       # the apple
    return finish(im)


def icon_bricks():
    im = canvas()
    d = ImageDraw.Draw(im)
    d.rounded_rectangle(box(4, 4, 44, 44), p(4), fill=(36, 42, 62))

    def cell(x, y, col):
        dark = tuple(int(c * 0.62) for c in col)
        light = tuple(min(255, int(c * 1.25)) for c in col)
        d.rectangle(box(x, y, x + 8.6, y + 8.6), fill=dark)
        d.rectangle(box(x + 0.8, y + 0.8, x + 7.8, y + 7.8), fill=col)
        d.line([(p(x + 0.8), p(y + 0.8)), (p(x + 7.8), p(y + 0.8))], fill=light, width=p(1))

    for x, y, col in [(9, 11, PURPLE), (17.6, 11, PURPLE), (26.2, 11, PURPLE), (17.6, 19.6, PURPLE),
                      (9, 31, TEAL), (17.6, 31, TEAL), (17.6, 22.4 + 8.6, TEAL),
                      (30, 31, AMBER), (30, 22.4 + 8.6, AMBER), (38.6 - 0.0, 31, AMBER)]:
        if x < 36:
            cell(x, y, col)
    return finish(im)


def icon_app():
    im = canvas()
    d = ImageDraw.Draw(im)
    fn = lambda dd: dd.rounded_rectangle(box(6, 6, 42, 42), p(8), fill=255)
    grad(im, (p(6), p(6), p(42), p(42)), (110, 160, 230), (60, 104, 190), fn)
    gloss(im, fn, 60)
    d.ellipse(box(16, 16, 32, 32), outline=(255, 255, 255), width=p(2.4))
    return finish(im)


ICONS = [
    ("app", icon_app), ("files", icon_files), ("notes", icon_notes),
    ("calc", icon_calc), ("clock", icon_clock), ("sysinfo", icon_sysinfo),
    ("tasks", icon_tasks), ("term", icon_term), ("chess", icon_chess),
    ("mines", icon_mines), ("reversi", icon_reversi), ("snake", icon_snake),
    ("bricks", icon_bricks),
]


def emit(path):
    out = open(path, "w")
    out.write("// SPDX-License-Identifier: GPL-2.0-only\n"
              "/* Generated by scripts/genicons.py: do not edit by hand. */\n"
              "#include <hix/types.h>\n\n")
    out.write("#define THEME_ICON_N %d\n#define THEME_ICON_PX %d\n\n" % (len(ICONS), N))
    out.write("const char *const theme_icon_names[%d] = {\n" % len(ICONS))
    for name, _ in ICONS:
        out.write('\t"%s",\n' % name)
    out.write("};\n\n")
    out.write("/* straight alpha, 0xAARRGGBB */\n")
    out.write("const u32 theme_icon_px[%d][%d] = {\n" % (len(ICONS), N * N))
    for name, fn in ICONS:
        px = fn().load()
        out.write("\t{ /* %s */\n" % name)
        for y in range(N):
            row = []
            for x in range(N):
                r, g, b, a = px[x, y]
                row.append("0x%02X%02X%02X%02X" % (a, r, g, b))
            out.write("\t\t" + ", ".join(row) + ",\n")
        out.write("\t},\n")
    out.write("};\n")
    out.close()


def sheet(path):
    cols = 5
    rows = (len(ICONS) + cols - 1) // cols
    img = Image.new("RGB", (cols * 112, rows * 112), (221, 221, 221))
    for i, (_, fn) in enumerate(ICONS):
        ic = fn().resize((96, 96), Image.LANCZOS)
        img.paste(ic, ((i % cols) * 112 + 8, (i // cols) * 112 + 8), ic)
    img.save(path)


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--sheet":
        sheet(sys.argv[2])
    else:
        emit(sys.argv[1] if len(sys.argv) > 1 else "steel/icon_data.c")
