# Roadmap

Each stage is verified by host tests and `make shots` frames; booting in QEMU
is checked separately (`make smoke`, and `make release` for a release), so
after every release you still need to verify it on your own hardware.

## 0.4.0 — done
Gantry (registry, drivers), framebuffer in RAM, sprite dragging.

## 0.5.0 — ring 3 (stage 1 done: address spaces, ELF, int 0x80, hello3/fault3/probe3; next: libc shim, fork/exec-like launch, moving programs)
- separate address spaces (per-process page directories, TSS.esp0);
- ELF loader, user programs via `int 0x80`;
- moving qubix programs (shell, tasks, ...) to ring 3 one by one;
- validating pointers from user memory instead of `user_ok` on a "local" address.

## 0.6.0 (partially done: Kit, services, SDK, /bin; a graphical app remains) — Kit (SDK)
- a library for user programs: window, widgets, events, pixel canvas;
- the first graphical application written with Kit, outside the kernel.

## 0.6.1 — done: Rust in the kernel and cipher verification
- optional `no_std` module `rust/hix-core` (ChaCha20-Poly1305), C ABI,
  `SYS_crypto` (601), `hx_crypto()` in the SDK, `cryptotest`;
- Python reference `scripts/crypto_check.py` (`make crypto-test`) and a self-test at
  boot; without i686-core the kernel still builds and the call returns `-ENOSYS`;
- next: replace the example with a cipher from another team and, if needed, move
  the Python interpreter to ring 3 (MicroPython) rather than ring 0.

## 1.0.0 — done: stable public release
- boot experience: splash, quiet VGA, four screensavers, idle autostart,
  preserving an unsubmitted shell line; configuration `config/hix.conf` →
  `build/hixconfig.h` via `scripts/hixctl`;
- runtime parameters `hix.splash`, `hix.quiet`, `hix.screensaver`,
  `hix.timeout`, `hix.safe`; sysctl `hix.boot` and `kern.cmdline`;
- GRUB menu with normal, verbose, no-splash, safe and serial entries; COM1 is
  a full shell;
- one version in `include/hix/version.h`; `make version`, `verify`, `smoke`,
  `release`; a clean strict build with Rust and ISO, QEMU smoke, `dist/` with
  `SHA256SUMS`, `BUILD-INFO` and sources;
- fixed `GANTRY_LOOKUP`, `%-12s` in `hx_printf`, and smoke-test races;
- added LICENSE, INSTALL/RELEASE/VERSIONING, CONTRIBUTING, SECURITY,
  release notes and CI;
- verified: strict build, `make verify`, `smoke --require-rust`; text
  about 218 KiB with Rust.

## 1.1.0 and beyond — glass
- in steel: transparency, gradients, blur/glare, soft-edged shadows, animations
  (appear, minimize), a layer compositor instead of the cell grid;
- a Frutiger Aero-style theme in desk under its own name.
