#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Render every desktop snapshot of the host tests to PNG, plus a contact sheet.

Usage: scripts/shots.py [outdir]        (default: build/shots)

Runs tests/build/test_desk with DESK_SHOTS set, converts the PPM frames (the
pixel desktop exactly as steel draws it) to PNG and writes index.png with the
frames side by side. This is how the look of the theme is checked without
booting anything.
"""
import glob
import os
import subprocess
import sys

from PIL import Image, ImageDraw

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "build", "shots"))
os.makedirs(out, exist_ok=True)

subprocess.run(["make", "-C", os.path.join(root, "tests"), "build/test_desk"], check=True,
               stdout=subprocess.DEVNULL)
env = dict(os.environ, DESK_SHOTS=out)
subprocess.run([os.path.join(root, "tests", "build", "test_desk")], check=True, env=env,
               stdout=subprocess.DEVNULL)

frames = []
for ppm in sorted(glob.glob(os.path.join(out, "*.ppm"))):
    png = ppm[:-4] + ".png"
    Image.open(ppm).save(png)
    os.remove(ppm)
    for scr in glob.glob(ppm[:-4] + ".scr"):
        os.remove(scr)
    frames.append(png)

cols, tw, th = 4, 384, 288
rows = (len(frames) + cols - 1) // cols
sheet = Image.new("RGB", (cols * tw, rows * (th + 18)), (30, 30, 34))
d = ImageDraw.Draw(sheet)
for i, f in enumerate(frames):
    im = Image.open(f).resize((tw - 8, th - 6), Image.LANCZOS)
    x, y = (i % cols) * tw + 4, (i // cols) * (th + 18) + 3
    sheet.paste(im, (x, y))
    d.text((x, y + th - 4), os.path.basename(f)[:-4], fill=(200, 200, 205))
sheet.save(os.path.join(out, "index.png"))
print("%d frames in %s" % (len(frames), out))
