#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Boot hix in QEMU and check the shell over COM1.

The 16550 driver mirrors the qubix tty to the serial port, so this script can
wait for the prompt, type commands and assert their output without a display.
It is the `make smoke` target and the last gate before a release package.

Usage: scripts/smoke.py [--elf build/hix.elf] [--require-rust]
                        [--append "hix.quiet=0"] [--no-shots]
"""
import argparse
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROMPT = "hix:/#"


class SmokeError(RuntimeError):
    pass


def clean(data):
    """A tiny terminal: CR, backspace and erase-to-end-of-line only.

    The shell redraws prompt+line on every keystroke, which is right for a
    real terminal but unreadable in a log if CR is simply dropped.
    """
    text = data.decode("utf-8", "replace")
    lines = []
    cur = ""
    col = 0
    i = 0

    while i < len(text):
        ch = text[i]
        if ch == "\r":
            col = 0
        elif ch == "\n":
            lines.append(cur)
            cur = ""
            col = 0
        elif ch == "\b":
            if col:
                col -= 1
        elif ch == "\x1b":
            i += 1
            if i < len(text) and text[i] == "[":
                i += 1
                params = ""
                while i < len(text) and not text[i].isalpha():
                    params += text[i]
                    i += 1
                if i < len(text) and text[i] == "K":
                    cur = cur[:col]
                elif i < len(text) and text[i] == "J" and params == "2":
                    # keep the scrollback: a serial log is not a screen
                    if cur:
                        lines.append(cur)
                    cur = ""
                    col = 0
                i += 1
            continue
        else:
            if len(cur) < col:
                cur += " " * (col - len(cur))
            cur = cur[:col] + ch + cur[col + 1:]
            col += 1
        i += 1
    lines.append(cur)
    return "\n".join(lines)


def read_version():
    with open(os.path.join(ROOT, "include", "hix", "version.h"),
              encoding="utf-8") as f:
        match = re.search(r'#define HIX_RELEASE\s+"([^"]+)"', f.read())
    if not match:
        raise SmokeError("cannot read HIX_RELEASE from include/hix/version.h")
    return match.group(1)


def connect(path, timeout):
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            sock.connect(path)
            sock.settimeout(0.5)
            return sock
        except OSError as err:
            last = err
            sock.close()
            time.sleep(0.05)
    raise SmokeError("cannot connect to %s: %s" % (path, last))


class Serial:
    def __init__(self, path, timeout):
        self.sock = connect(path, timeout)
        self.raw = b""
        self.lock = threading.Lock()
        self.alive = True
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def _read(self):
        while self.alive:
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                continue        # an idle serial line, not a closed one
            except OSError:
                break
            if not data:
                break
            with self.lock:
                self.raw += data

    def mark(self):
        with self.lock:
            return len(self.raw)

    def text_since(self, start):
        with self.lock:
            data = self.raw[start:]
        return clean(data)

    def send(self, text):
        self.sock.sendall(text.encode("utf-8"))

    def wait_for(self, needle, timeout, start=0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if needle in self.text_since(start):
                return self.text_since(start)
            time.sleep(0.05)
        raise SmokeError("timeout waiting for %r\n%s" %
                         (needle, self.tail(start)))

    def wait_all(self, needles, timeout, start=0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            text = self.text_since(start)
            if all(n in text for n in needles):
                return text
            time.sleep(0.05)
        raise SmokeError("timeout waiting for %s\n%s" %
                         (", ".join(repr(n) for n in needles),
                          self.tail(start)))

    def tail(self, start=0, lines=80):
        return "\n".join(self.text_since(start).splitlines()[-lines:])

    def close(self):
        self.alive = False
        try:
            self.sock.close()
        except OSError:
            pass


class Monitor:
    def __init__(self, path, timeout):
        self.sock = connect(path, timeout)
        self.sock.settimeout(1.0)
        self.lock = threading.Lock()

    def command(self, text):
        with self.lock:
            self.sock.sendall((text + "\n").encode("ascii"))
            try:
                self.sock.recv(4096)
            except OSError:
                pass

    def screendump(self, path, timeout=4):
        try:
            os.unlink(path)
        except FileNotFoundError:
            pass
        self.command("screendump " + path)
        deadline = time.time() + timeout
        while time.time() < deadline:
            if os.path.exists(path) and os.path.getsize(path) > 0:
                return
            time.sleep(0.05)
        raise SmokeError("QEMU did not write %s" % path)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def run(serial, command, expects, timeout=20):
    start = serial.mark()
    serial.send(command + "\n")
    serial.wait_all(expects, timeout, start)
    serial.wait_for(PROMPT, timeout, start)
    return serial.text_since(start)


def fail(message, transcript=None):
    raise SmokeError(message + ("\n" + transcript if transcript else ""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", default=os.path.join(ROOT, "build", "hix.elf"))
    parser.add_argument("--require-rust", action="store_true",
                        help="fail when the kernel was built without Rust crypto")
    parser.add_argument("--append", default="",
                        help="Multiboot command line passed to the kernel")
    parser.add_argument("--no-shots", action="store_true")
    parser.add_argument("--logdir", default=os.path.join(ROOT, "build", "smoke"))
    parser.add_argument("--boot-timeout", type=float, default=30.0)
    args = parser.parse_args()

    version = read_version()
    qemu = shutil.which("qemu-system-i386")
    if not qemu:
        sys.exit("smoke: qemu-system-i386 not found")
    if not os.path.exists(args.elf):
        sys.exit("smoke: %s not found (run make first)" % args.elf)

    os.makedirs(args.logdir, exist_ok=True)
    shots = os.path.join(args.logdir, "shots")
    if not args.no_shots:
        os.makedirs(shots, exist_ok=True)

    tmp = tempfile.mkdtemp(prefix="hix-smoke-")
    serial_path = os.path.join(tmp, "serial.sock")
    monitor_path = os.path.join(tmp, "monitor.sock")
    qemu_log = os.path.join(tmp, "qemu.log")
    cmd = [qemu, "-m", "128M", "-kernel", args.elf, "-display", "none",
           "-vga", "std", "-no-reboot",
           "-serial", "unix:%s,server=on,wait=on" % serial_path,
           "-monitor", "unix:%s,server=on,wait=on" % monitor_path]
    if args.append:
        cmd += ["-append", args.append]

    serial = monitor = None
    proc = None
    log = open(qemu_log, "wb")
    try:
        proc = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT)
        # wait=on makes QEMU block until the chardev is connected; it blocks
        # on the monitor first, so the serial socket only appears after it.
        monitor = Monitor(monitor_path, 10)
        serial = Serial(serial_path, 10)

        # Early printk may be dropped before the socket is fully up; ask the
        # shell for the ring buffer instead.
        serial.wait_for(PROMPT, args.boot_timeout)
        run(serial, "dmesg", ["boot complete", "boot config:"], timeout=10)
        print("  ok  boot log (dmesg)")

        if not args.no_shots:
            serial.send("splash 4\n")
            time.sleep(0.5)     # let the command echo and splash start
            mark = serial.mark()
            monitor.screendump(os.path.join(shots, "splash.ppm"))
            serial.send("x")    # any key skips the splash
            serial.wait_for(PROMPT, 10, mark)
            print("  ok  splash renders and skips")

        run(serial, "uname -a", ["hix " + version], timeout=10)
        print("  ok  uname reports hix " + version)

        crypto_text = run(serial, "crypto", ["crypto:"], timeout=25)
        crypto_ok = "self-test ok" in crypto_text
        if not crypto_ok:
            if args.require_rust:
                fail("crypto self-test did not pass", crypto_text)
            if "not built" not in crypto_text:
                fail("unexpected crypto output", crypto_text)
        print("  ok  crypto")

        # Wait for the output, not for the prompt: the echoed command line
        # already contains "hix:/#" and would make wait_for() return at once.
        expect = ["selftest ok"] if crypto_ok else ["unavailable"]
        text = run(serial, "sysctl hix.crypto", expect, timeout=10)
        if args.require_rust and "selftest ok" not in text:
            fail("kernel has no working Rust crypto", text)
        print("  ok  sysctl hix.crypto")

        run(serial, "progs", ["splash", "screensaver", "init"], timeout=10)
        print("  ok  progs lists splash and screensaver")

        run(serial, "kitinfo", ["running in ring 3",
                                "user.last = kitinfo"], timeout=15)
        print("  ok  kitinfo (Kit from ring 3)")

        run(serial, "lsbin", ["lsbin: argc=1, listing /bin", "kitinfo"],
            timeout=15)
        print("  ok  lsbin (argv and getdirentries)")

        run(serial, "fstest", ["fstest: read 20 bytes",
                               "primes below 2000: 303",
                               "fstest: child", "exited with 0"], timeout=25)
        print("  ok  fstest (VFS and spawn/wait)")

        # "cryptotest:" alone would match the command echo and race ahead.
        if crypto_ok:
            text = run(serial, "cryptotest",
                       ["cryptotest: all checks passed"], timeout=25)
            if "all checks passed" not in text:
                fail("cryptotest did not pass", text)
        else:
            text = run(serial, "cryptotest",
                       ["cryptotest: self-test: -38"], timeout=25)
        print("  ok  cryptotest")

        run(serial, "hixfetch", ["OS: hix " + version,
                                 "Kernel: aether + qubix + steel",
                                 "DE: hix Desktop"], timeout=10)
        print("  ok  hixfetch")
        if not args.no_shots:
            time.sleep(0.2)
            monitor.screendump(os.path.join(shots, "shell.ppm"))

        serial.send("screensaver blank\n")
        time.sleep(0.6)         # let the command echo settle first
        mark = serial.mark()
        time.sleep(1.0)
        if PROMPT in serial.text_since(mark):
            fail("screensaver returned before a key was pressed")
        if not args.no_shots:
            monitor.screendump(os.path.join(shots, "screensaver.ppm"))
        serial.send("x\n")
        serial.wait_for(PROMPT, 10, mark)
        print("  ok  screensaver starts and wakes on a key")

        serial.send("halt\n")
        try:
            proc.wait(timeout=8)
        except subprocess.TimeoutExpired:
            monitor.command("quit")
            proc.wait(timeout=5)
        print("  ok  halt")
        print("smoke: all checks passed (hix %s)" % version)
        return 0
    except SmokeError as err:
        print("smoke: FAILED: %s" % err, file=sys.stderr)
        if serial:
            print("---- serial tail ----", file=sys.stderr)
            print(serial.tail(), file=sys.stderr)
        if os.path.exists(qemu_log):
            data = open(qemu_log, "rb").read().decode("utf-8", "replace")
            if data.strip():
                print("---- qemu ----", file=sys.stderr)
                print(data[-2000:], file=sys.stderr)
        return 1
    finally:
        if serial:
            with open(os.path.join(args.logdir, "serial.log"), "wb") as out:
                out.write(serial.raw)
            serial.close()
        if monitor:
            monitor.close()
        if proc and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
        log.close()
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
