# Version history

## 1.0.0 — first public release after 0.3.4
Includes local 0.3.5–0.6.1 and brings boot, the ring 3 ABI and release
engineering to a state that can be promised to users. Detailed notes —
[docs/releases/1.0.0.md](releases/1.0.0.md), versioning scheme —
[docs/VERSIONING.md](VERSIONING.md).

- **Splash screen, quiet boot, screensavers** (see below): splash with a wide
  block logo, UTC clock, date, RAM, Rust status and progress; four
  screensavers; quiet mode filters only VGA info/notice, `dmesg` and COM1
  remain complete.
- **Host configuration**: `config/hix.conf` → `build/hixconfig.h` via
  `scripts/hixctl`; `make config`, `make` regenerates the header.
- **Runtime overrides**: `hix.splash=0|on|N`, `hix.quiet=0|1`,
  `hix.screensaver=NAME|off`, `hix.screensaver_timeout=N`/`hix.timeout=N`,
  `hix.safe=1`; parsing in `kernel/bootconfig.c`, sysctl `hix.boot` and
  `kern.cmdline`.
- **GRUB menu**: normal, verbose, no splash, safe mode and serial console
  on COM1 115200; COM1 is a full shell (tty mirror in `qubix/tty.c`).
- **One version**: `include/hix/version.h` is the only source;
  `HIX_RELEASE`/`HIX_EXPAND` in `include/hix/hix.h`; old `0.2.0`/`0.6.1` were
  removed from code, README, SDK and tests.
- **Build and verification**: `make version|verify|smoke|release`; `verify`
  runs style, Multiboot, host tests, Rust, Python vectors and frames;
  `release` does a clean strict build with Rust and ISO, `verify`, QEMU smoke
  and a package `dist/hix-1.0.0/` with ELF/ISO/docs/logs, `BUILD-INFO` and
  `SHA256SUMS`. The script is `scripts/release.sh`.
- **Fixes**: `GANTRY_LOOKUP` took the service name from `ubuf` instead of
  `arg` (`kitinfo: kit.system not found`); `hx_printf` did not support
  `%-12s` and shifted arguments (`lsbin` crash); the smoke test got correct
  handling of serial/monitor, timeouts and early screensaver return;
  `parse_seconds` ignores garbage instead of substituting the maximum.
- **Documents**: `LICENSE` (GPL-2.0-only), `CONTRIBUTING.md`,
  `SECURITY.md`, `docs/INSTALL.md`, `docs/RELEASE.md`,
  `docs/VERSIONING.md`, `docs/releases/1.0.0.md`, CI in
  `.github/workflows/ci.yml`.
- Verified: `make STRICT=1 RUST=1 all`, `make verify`,
  `python3 scripts/smoke.py --require-rust`; text about 218 KiB with Rust.

## After 0.6.1 (included in 1.0.0) — splash, configuration and screensavers
- **Boot without a log flood.** `config/hix.conf` sets splash, its duration,
  VGA quiet mode, the screensaver and its timeout. `scripts/hixctl` is a
  bash panel on the host: `show`, `set`, `splash`, `quiet`, `screensaver`,
  `build`, `run`, `check`, `edit`, `reset`, `doctor`. `make` itself generates
  `build/hixconfig.h` via `include/hix/bootconfig.h`; host tests without it
  get the old default values.
- **Splash screen** (`qubix/splash.c`, `include/qubix/splash.h`): logo,
  version, large UTC clock in block digits, date/weekday, RAM and Rust crypto
  module status, progress bar, any key to skip. `init` starts it once before
  the first `sh`; afterwards the shell prints a compact header.
- **Quiet boot** (`steel/console.c`): with `quiet = on` the VGA console skips
  info/notice (levels 5–6), warnings and errors remain. `dmesg` and serial
  still receive everything; the steel console's `.max_level` is set at
  registration.
- **Screensavers** (`qubix/screensaver.c`): `stars`, `matrix`, `clock`,
  `blank` — ordinary qprog programs with raw input and `TTY_SETTIMEOUT`; any
  key exits. The shell starts the selected one after `screensaver_timeout`
  seconds of idle, preserving an unsubmitted line, and via the command
  `screensaver [NAME]`.
- QEMU: captured frames of the splash screen, matrix, stars, clock and blank;
  verified key skip, idle autostart and return to the shell. Build: text
  217,666 bytes with Rust. Tests: `make test`, `make check`,
  `make crypto-test`, `make rust-test`.
- Documentation: [docs/BOOT.md](BOOT.md), updated README, ARCHITECTURE,
  ROADMAP.

## 0.6.1 — technical part: argv, system calls, Tweaks
- Ring 3 programs get `main(argc, argv)` (System V stack layout, checked in
  `test_uspace`).
- The ring 3 syscall whitelist is extended with verified calls: open, lseek,
  mkdir, unlink, getdirentries, gettimeofday, wait4, posix_spawn, kill,
  sysctlbyname, sleep, yield. Graphics, ports, ioctl, reboot are closed.
- libhix: files and directories, spawn/wait, sleep, time, sysctl,
  `hx_read_file`, `hx_write_file`; examples `lsbin`, `fstest`.
- **Tweaks** (`include/gantry/tweak.h`, `gantry/tweak.cpp`, `gantry/tweaks/`):
  kernel extensions in C++, registered with `GANTRY_TWEAK()`, enabled and
  disabled at runtime (`tweak`, `tweak on|off name`, service `kit.tweak`,
  `sysctl hix.tweaks`). Examples: `procwatch`, `procstats`.
- `docs/KERNEL-LIB.md`: how to write code for the kernel (program, tweak,
  kernel).
- **Rust in the kernel** (`rust/hix-core`, optional): ChaCha20-Poly1305 per
  RFC 8439, `no_std`, integer arithmetic only, panic through the kernel
  `panic()`. `SYS_crypto` (601) with the ABI in `include/hix/cryptoabi.h`;
  `qubix/crypto.c` validates ring 3 pointers, copies buffers and wipes keys.
  Without i686-core the kernel builds and the call returns `-ENOSYS`. Linked
  with `--gc-sections`, so unused `core` functions do not enter the image.
- SDK: `hx_crypto()`, program `cryptotest` (self-test, seal/open, tag
  forgery); shell: `crypto`, `sysctl hix.crypto`; at boot a line
  `qubix: Rust crypto up (ChaCha20-Poly1305)`.
- Python reference: `scripts/crypto_check.py` (`make crypto-test`) generates
  vectors via `cryptography` and compares them with the same C ABI; 31 cases.
  `make rust-test` — 6 crate unit tests. Build: `make RUST=0|1`,
  `make rust-sysroot`.
- Verified in QEMU: boot to the scheduler, `crypto`, `sysctl hix.crypto`,
  `cryptotest: all checks passed`.
- Tests: gantry 113, uspace 58.

## 0.6.0 — Gantry complete: Kit, services, SDK
- **Gantry** (`gantry/kit.cpp`, `include/gantry/kit.h`): service table
  (`Service`, `Services` node in the registry), notification center
  (`Notifier`, subscribers, topic counters), property tree (`Properties`) and
  three built-in Kits: `kit.system`, `kit.prop`, `kit.notify`. Kernel events:
  `proc.spawn`, `proc.exit`, `gantry.kits`.
- `SYS_gantry` (600): service lookup and call; ABI in `include/hix/kitabi.h`.
- **SDK** (`user/sdk`, `user/apps`, `scripts/mkapps.py`, `docs/SDK.md`):
  libhix, crt0, building C programs into ELF for ring 3, initfs in the kernel
  → `/bin`, launch from the shell by name. Examples: `kitinfo`, `primes`.
- The shell launches any program by name (built-in or from `/bin`).
- Tests: gantry 81 checks (services, property write permissions, table
  overflows), `test_sdk` (real compiler → initfs → VFS → ELF parsing,
  corrupted archives).
- Not done: graphical application outside the kernel, argv in ring 3, more
  system calls for ring 3.

## 0.5.0 — desk and shell fixes (stage 1)
- desk: the bottom row was removed; only the top dock remains (menus, window
  buttons, clock).
- Windows can be moved past the left and right screen edges (they stay
  visible for at least 4 cells so the window can be brought back).
- Shell: `exit` no longer causes an infinite restart (the static `want_exit`
  flag was not reset between launches, so pid grew to 22000). `init`
  additionally waits 200 ms between shell restarts.
- **Ring 3, stage 1** (`include/aether/uspace.h`, `aether/uspace*.c`,
  `lib/elf.c`, `qubix/uexec.c`, `user/*.asm`): a process has its own page
  directory; the kernel part is shared and supervisor-only, the user window
  0x40000000–0x7FFFFFFF is private (U bit). The ELF32 loader validates all
  offsets and sizes; thread switching changes CR3 and TSS.esp0
  (`arch_thread_switch`); entry via `iret` (`enter_user`); a user program
  fault kills the process (status 139), not the kernel. Ring 3 system calls:
  exit, read, write, close, getpid, getppid, the rest return -ENOSYS;
  pointers are validated against the process address space.
- Demo assembly programs embedded in the image: `hello3`, `fault3` (reads
  kernel memory), `probe3` (bad pointer, foreign call, `cli`). Built-in qubix
  programs still run in ring 0.
- Tests: `test_uspace` (32-bit, 42 checks): ELF parsing with corrupted
  headers, address space construction, page boundaries, write permissions,
  out-of-memory without leaks.

## 0.4.0 — Gantry and smooth dragging
- **Gantry** (`gantry/`, `include/gantry/gantry.h`, C++): device registry,
  driver model with score-based matching, built-in GenericPCI and BochsDisplay
  drivers, `gantry_boot()` from `kernel/main.c`,
  `sysctl hix.registry`/`hix.platform`, shell command `registry`, Platform line in
  `hixfetch`. The kernel is still pure C.
- Rendering: `pixdev` draws into a RAM copy of the screen and writes only the
  changed row segments to video memory; the cursor is composited on top
  (without saving the background).
- A window being dragged is a pixel sprite (`pixdev_float`, `STEEL_CTL_FLOAT`):
  it moves exactly with the cursor, not in 12x28 cells; with a shadow, and on
  release the window snaps to the grid. For windows with pixel clients
  (Chess, Clock) the previous mode applies. Dragging a 720x448 window on the
  host: about 0.4 ms versus 1 ms when redrawing cells.
- The `class` field in `struct hix_driver` was renamed to `kind` (`class`
  cannot be used in C++).
- Tests: gantry 40+, desk (the sprite matches the screen byte for byte,
  grid snapping).
- `docs/ROADMAP.md`: ring 3, Kit/SDK, Frutiger Aero.

## 0.3.6 — panels and starry sky (includes 0.3.5)
- A GNOME 2-inspired desktop, drawn in pixels in the style of Mac OS 9: top
  panel (hix logo, Apps and Computer menus, clock) and bottom panel with a
  button per window (24x24 icon, active window "pressed in", minimized
  dimmed). New `steel/panel.cpp` (C++), `include/steel/panel.h`,
  `pixdev_panel`, `STEEL_CTL_PANEL`; the cell grid moved to y=24.
- New wallpaper: procedural starry sky, Milky Way, mesa silhouettes.
- Font `gfx_font_ubs` (12 px, bold) for panels; `scripts/genfont.py` updated.
- desk: global Apps/Computer menus (mouse and F9), window button click,
  Restart/Shut Down with confirmation, Exit Desktop; the backend gained
  `panel`, `wall_time`, `power`.
- Fixed: the window drop-down menu lost item text (the frame overwrote the
  cell role); mouse button state carried over between desk sessions.
- Kernel: `kill(pid)` (flag, termination on the next system call; pids 0 and 1
  protected), `kill` command in the shell and Term; sysctl `hw.display`,
  `hix.desktop`.
- `hixfetch` (`lib/hixfetch.c`): logo and system information, like screenfetch;
  the command is in the shell and Term. Term reduced to 66x19, references to
  other systems removed.
- Rust: `tools/shotsum` — desktop frame checksums (`make shots-golden`,
  `make shots-check`).
- Tests: desk 92 checks (panels, menus, window buttons, Restart/Shut Down,
  hixfetch, kill).

## 0.3.4 — Argent theme
- The steel cell became 32-bit: the low 16 bits are as in VGA (character +
  color), the high bits are the cell role (`STEEL_RCELL`, `steel_tag`,
  `steel_shade`). The text display ignores the role.
- New theme in C++ (`steel/theme.cpp`, `include/steel/theme.h`): wallpaper,
  windows with striped title bars and close/maximize/minimize buttons, frame
  bevels, soft window and menu shadows, menu bar, drop-down menus, rounded
  buttons, 48x48 icons and labels (with text remembered under overlapping
  windows).
- The 16-color palette was softened (`pixdev.c`).
- Icons are generated by `scripts/genicons.py` (Python, Pillow) into
  `steel/icon_data.c`; `scripts/shots.py` / `make shots` render all desktop
  frames to PNG and a contact sheet.
- Clock in pixel mode: a smooth dial with hands (`gfx_line16`, anti-aliased
  line), the digital mode is also pixel-based.
- `include/hix/list.h`: the `new` parameter was renamed (the header can be
  included from C++).
- Tests: gfx 67 (line and theme), desk runs on the pixel display.

## 0.3.3 (includes 0.3.2)
- Fixed a hang when starting Clock: an infinite loop in the line algorithm
  (Bresenham) for hands at some angles. Added a test that draws the clock at
  100+ moments in time.
- Fixed a black screen after exiting desk: switching the adapter to graphics
  changes VGA registers and overwrites the text-mode font. The font is now
  saved before entering graphics, and on exit mode 80x25 is reprogrammed and
  the font is restored (`vga_font_save`, `vga_text_restore`).
- BGA: the virtual height is set equal to the visible height (reduces the
  risk of stretching the image in a full-screen QEMU window).
- C++ for layers above the kernel: `CXX` build (without exceptions, RTTI and
  the standard library), `lib/cxxrt.c`, `lib/new.cpp` (new/delete over
  kmalloc), global object constructors via `.init_array`. The hix kernel
  remains C. The first C++ module is the chess rules (`apps/chess_rules.cpp`).
- Tests: test_cxx (7), desk 64.

## 0.3.1
- Names: references to other systems removed; layers — hix, aether, qubix,
  steel, desk; desktop programs: Hub, Files, Notes, Term, Minefield, Bricks.
- steel: 1024x768x32 pixel mode (Bochs VBE over PCI), canvases with alpha,
  smooth fonts, pixel cursor, pixel clients over the cell grid.
- Chess: full rules, engine with 4 levels, interface with dragging,
  animation, clock, evaluation bar and game record.
- shell: line editor, history, Tab, variables, quotes, `;`, `>`/`>>`,
  cwd and new commands (cd, pwd, cp, mv, touch, wc, head, grep, top, free …).
- qubix: raw tty mode and `ioctl`.
- Tests: shell 111, gfx 55, chess 1602 (perft), desk 63.

## 0.3.0
- Desktop with games.

## 0.2.0
- Processes, syscalls, VFS, ports and IPC, scheduler.

## 0.1.0
- hi-Kernel: boot, GDT/IDT/PIC, PMM, keyboard, simple shell.
