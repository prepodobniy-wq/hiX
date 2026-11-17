#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check that the pixel desktop really reaches the QEMU display.

Boots hix with a chosen -vga adapter, asks the kernel what display it found,
starts desk and dumps the QEMU console. A pixel desktop is 1024x768 with
thousands of colours; falling back to VGA text gives the 720x400 text surface.

Usage: scripts/gfxcheck.py [--vga std|vmware|cirrus] [--elf build/hix.elf]
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import smoke  # noqa: E402  (the serial and monitor helpers of the smoke test)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# what sysctl hw.display must say, and the QEMU console size
MODES = {
    "std": ("1024x768, bochs vbe", (1024, 768)),
    "vmware": ("1024x768, vmware svga", (1024, 768)),
    "cirrus": ("80x25, vga text", (720, 400)),
}


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"P6"):
        raise smoke.SmokeError("%s is not a binary PPM" % path)
    tokens = []
    pos = 0
    while len(tokens) < 4:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while pos < len(data) and data[pos:pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        tokens.append(data[start:pos])
    pos += 1
    width, height, maxval = (int(t) for t in tokens[1:])
    if maxval != 255:
        raise smoke.SmokeError("%s: unsupported maxval %d" % (path, maxval))
    pixels = data[pos:pos + width * height * 3]
    if len(pixels) != width * height * 3:
        raise smoke.SmokeError("%s: truncated pixels" % path)
    colors = {pixels[i:i + 3] for i in range(0, len(pixels), 3)}
    return width, height, len(colors), pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vga", choices=sorted(MODES), default="vmware")
    parser.add_argument("--elf", default=os.path.join(ROOT, "build", "hix.elf"))
    parser.add_argument("--outdir", default=os.path.join(ROOT, "build", "gfxcheck"))
    parser.add_argument("--boot-timeout", type=float, default=30.0)
    args = parser.parse_args()

    qemu = shutil.which("qemu-system-i386")
    if not qemu:
        sys.exit("gfxcheck: qemu-system-i386 not found")
    if not os.path.exists(args.elf):
        sys.exit("gfxcheck: %s not found (run make first)" % args.elf)

    expect, size = MODES[args.vga]
    os.makedirs(args.outdir, exist_ok=True)
    shot = os.path.join(args.outdir, args.vga + ".ppm")

    tmp = tempfile.mkdtemp(prefix="hix-gfx-")
    serial_path = os.path.join(tmp, "serial.sock")
    monitor_path = os.path.join(tmp, "monitor.sock")
    cmd = [qemu, "-m", "128M", "-kernel", args.elf, "-display", "none",
           "-vga", args.vga, "-no-reboot",
           "-serial", "unix:%s,server=on,wait=on" % serial_path,
           "-monitor", "unix:%s,server=on,wait=on" % monitor_path]

    serial = monitor = proc = None
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                stderr=subprocess.STDOUT)
        monitor = smoke.Monitor(monitor_path, 10)
        serial = smoke.Serial(serial_path, 10)
        serial.wait_for(smoke.PROMPT, args.boot_timeout)
        smoke.run(serial, "sysctl hw.display", [expect], timeout=10)
        print("  ok  %s: hw.display = %s" % (args.vga, expect))

        serial.send("desk\n")
        time.sleep(2.0)         # let desk draw its first frame
        monitor.screendump(shot, timeout=8)
        print("  ok  %s: desk frame saved to %s" % (args.vga, shot))
        if size[0] == 1024:
            # F9 opens the Apps menu: the second frame proves that desk
            # redraws really reach the host after the initial mode set.
            menu_shot = os.path.join(args.outdir, args.vga + "-menu.ppm")
            monitor.command("sendkey f9")
            time.sleep(0.8)
            monitor.screendump(menu_shot, timeout=8)
            print("  ok  %s: menu frame saved to %s" % (args.vga, menu_shot))

            # Leave desk: the pixel mode must hand the screen back to VGA text.
            monitor.command("sendkey esc")
            mark = serial.mark()
            monitor.command("sendkey alt-f4")
            time.sleep(0.6)
            monitor.command("sendkey ret")
            serial.wait_for(smoke.PROMPT, 10, mark)
            exit_shot = os.path.join(args.outdir, args.vga + "-text.ppm")
            monitor.screendump(exit_shot, timeout=8)
            print("  ok  %s: desk exited back to text mode" % args.vga)
    finally:
        if monitor:
            monitor.command("quit")
            monitor.close()
        if serial:
            serial.close()
        if proc:
            try:
                proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        shutil.rmtree(tmp, ignore_errors=True)

    width, height, colors, pixels = read_ppm(shot)
    if (width, height) != size:
        sys.exit("gfxcheck: %s rendered %dx%d, expected %dx%d" %
                 (args.vga, width, height, size[0], size[1]))
    if size[0] == 1024:
        _, _, _, menu_pixels = read_ppm(menu_shot)
        if menu_pixels == pixels:
            sys.exit("gfxcheck: %s did not redraw after F9" % args.vga)
        if colors < 1000:
            sys.exit("gfxcheck: %s rendered only %d colours, "
                     "not a pixel desktop" % (args.vga, colors))
        ew, eh, _, _ = read_ppm(exit_shot)
        if (ew, eh) != (720, 400):
            sys.exit("gfxcheck: %s left a %dx%d screen after desk, "
                     "not VGA text" % (args.vga, ew, eh))
    print("gfxcheck: -vga %s renders %dx%d with %d colours" %
          (args.vga, width, height, colors))
    return 0


if __name__ == "__main__":
    sys.exit(main())
