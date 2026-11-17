# Installation and first boot

hix is an i386 kernel. "Installation" here means building `build/hix.elf` and
running it in QEMU directly (`-kernel`) or from a bootable ISO via GRUB.
Installing the system to disk is not needed yet: there is no disk filesystem
or installer.

## What you need on the host

Minimal set (Debian/Ubuntu):

    sudo apt-get install build-essential gcc-multilib g++-multilib nasm \
        python3 qemu-system-x86 grub-pc-bin grub-common xorriso

Fedora:

    sudo dnf install gcc gcc-c++ glibc-devel.i686 nasm python3 \
        qemu-system-x86 grub2-tools-extra xorriso

Arch:

    sudo pacman -S base-devel lib32-glibc nasm python3 qemu grub xorriso

Check that the compiler supports `-m32`:

    echo 'int main(void) { return 0; }' | gcc -m32 -x c - -o /tmp/m32 && /tmp/m32
    nasm -v
    qemu-system-i386 --version

The ISO needs `grub-mkrescue` (the `grub-pc-bin` / `grub2-tools-extra`
package) and `xorriso`; `make smoke` needs QEMU. Without `grub-mkrescue`, a
normal `make` still builds the ELF and honestly reports that the ISO was
skipped.

### Rust (optional for a normal build)

The Rust crypto module (`rust/hix-core`, ChaCha20-Poly1305) is the only
optional dependency. A normal `make` includes it if a `core` for
`i686-unknown-linux-gnu` is found; otherwise the kernel builds without it and
`SYS_crypto` returns `-ENOSYS`.

Option with rustup:

    rustup target add i686-unknown-linux-gnu

Debian/Ubuntu option without root (downloads `libstd-rust-dev:i386` into
`.rust-sysroot/`):

    scripts/hixctl doctor          # shows what is missing
    make rust-sysroot

A release build requires Rust explicitly: `make RUST=1` or `make release`.

## Building

    make                 # build/hix.elf + build/hix.iso (if GRUB is available)
    make strict          # clean and build with -Werror
    make verify          # host tests, style, Multiboot, Rust, crypto, frames
    make smoke           # boot in QEMU and check through COM1
    make version         # version from include/hix/version.h

Useful variables: `V=1` (show commands), `RUST=1|0|auto`,
`CROSS_COMPILE=i686-elf-`, `RUST_SYSROOT=/path/to/sysroot`.

## Running

    make run             # QEMU with -kernel, display window
    make run-serial      # the same, COM1 in the current terminal
    make run-iso         # boot via GRUB from build/hix.iso
    make debug           # QEMU with a gdb stub on :1234 and a stop at startup

Inside the shell:

    splash               # show the splash screen again
    screensaver matrix   # run the screensaver manually
    desk                 # desktop
    hixfetch             # system summary
    halt                 # power off the machine

## ISO boot menu

There are five entries in `iso/boot/grub/grub.cfg`:

| Entry | What it sets |
|---|---|
| hix | `hix.splash=on hix.quiet=on` |
| verbose boot | `hix.splash=on hix.quiet=0` |
| no splash, verbose | `hix.splash=0 hix.quiet=0` |
| safe mode | `hix.safe=1` (no splash, quiet or screensaver) |
| serial console | COM1 115200, all input/output to the terminal |

The same parameters can be passed manually: `-append 'hix.splash=0 hix.quiet=0'`
for `-kernel`, or via the `multiboot` line in GRUB. The full list is in
[docs/BOOT.md](BOOT.md).

## Configuration without rebuilding

`config/hix.conf` and `scripts/hixctl` set the defaults that are compiled into
the kernel. `hix.*` parameters on the boot command line override them for one
boot:

    make run                       # values from config/hix.conf
    qemu-system-i386 -m 128M -kernel build/hix.elf \
        -append 'hix.splash=4 hix.quiet=0 hix.screensaver=matrix hix.timeout=60'

`make` regenerates `build/hixconfig.h` itself; there is no need to run the
generator manually.

## If it does not boot

1. Build with full output: `make clean && make V=1`, then
   `make run-serial` and save the COM1 log.
2. Check `make check` (style and Multiboot header) and `make test`.
3. Try safe mode:
   `qemu-system-i386 -m 128M -kernel build/hix.elf -append 'hix.safe=1'` or
   the GRUB **safe mode** entry (without splash, quiet and screensaver). If it
   works, the problem is in early VGA/timers.
4. Make sure QEMU runs with `-vga std` (Bochs VBE, the default in `make run`)
   or `-vga vmware` (VMware SVGA II); both give the 1024x768x32 pixel mode.
   Other adapters keep the 80x25 text mode.
5. Send `build/smoke/serial.log` or the output of `make run-serial`.

`make smoke` is the fastest way to check that the kernel reaches the shell at
all: it starts QEMU without a window, connects to COM1 and runs `uname`,
`crypto`, `kitinfo`, `fstest`, `cryptotest`, `hixfetch`, the screensaver and
`halt`. `make gfxcheck` starts `desk` under QEMU with `-vga std`,
`-vga vmware` and `-vga cirrus` and checks the real console: pixel frames for
the first two, a redraw after F9 and a clean return to text mode after leaving
the desktop.

## Where to look next

- [README.md](../README.md) — project overview and features.
- [docs/BOOT.md](BOOT.md) — splash, quiet, screensavers, `hix.*`.
- [docs/ARCHITECTURE.md](ARCHITECTURE.md) — aether/qubix/steel/gantry layers.
- [docs/CRYPTO.md](CRYPTO.md) — Rust in the kernel and the `SYS_crypto` ABI.
- [docs/RELEASE.md](RELEASE.md) — how a release is built and published.
