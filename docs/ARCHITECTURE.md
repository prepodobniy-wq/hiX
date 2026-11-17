# hix architecture

    +--------------------------------------------------------------+
    |  qubix: shell, processes, VFS, tty, syscalls (int 0x80)      |
    +-----------------------------+--------------------------------+
    |  aether: task/thread, scheduler, ports/IPC, vm, heap         |  steel
    +-----------------------------+--------------------------------+  (graphics)
    |  drivers: vga, bga, pci, serial, pit, rtc, ps2 kbd/mouse     |
    +--------------------------------------------------------------+
    |  hix core: boot, GDT/TSS/IDT/PIC, traps, printk, panic       |
    +--------------------------------------------------------------+
    |  rust/hix-core: ChaCha20-Poly1305 via SYS_crypto (optional)  |
    +--------------------------------------------------------------+

## Boot (`kernel/main.c`)

GDT/IDT/PIC → consoles (serial + steel) → `pmm_init` → `vm_init` (paging) →
`kalloc_init` → `task_init`/`sched_init` (the boot context becomes the `idle`
thread) → drivers → `qubix_init` → thread `init` → `init` (pid 1) →
`splash` (once, if enabled) → `sh`.

Boot configuration (`config/hix.conf` → `build/hixconfig.h`) sets splash,
its duration, VGA quiet mode, and the screensaver with an idle timeout. On top
of that, `kernel/bootconfig.c` parses `hix.*` from the Multiboot command line
(`hix.splash`, `hix.quiet`, `hix.screensaver`, `hix.timeout`, `hix.safe`) and
exposes `sysctl hix.boot`/`kern.cmdline`. In quiet mode `steel/console.c` skips
info/notice, but `kernel/printk.c` still writes everything to the ring log and
to COM1; `dmesg` remains complete. COM1 mirrors the tty (`qubix/tty.c`), so
serial is a full console, not just a log. The splash screen and screensavers
(`qubix/splash.c`, `qubix/screensaver.c`) are ordinary qubix programs that draw
through `libq`/`int 0x80`.

## aether

- **pmm** — page bitmap, contiguous allocations.
- **vm** — 4 KiB pages: page 0 is unmapped (NULL → fault), `.text/.rodata`
  read-only, each thread stack has a guard page. Stack overflow is caught by a
  double fault on a separate TSS stack, which prints a diagnosis.
- **kalloc** — heap with boundary tags, magic checks, double free = panic.
- **task/thread/sched** — a task is a resource container (ports), a thread is
  execution; round-robin with a 50 ms quantum, preemption on interrupt exit,
  `ctx_switch` in `arch/x86/switch.asm`. Locking: `spin_lock_irqsave` (short)
  and `mutex` (long); waiting uses `waitq_wait_irq`.
- **ipc** — ports with SEND/RECEIVE rights, names with a "generation", messages
  with a reply port, timeouts, a bootstrap name service (`hix.upper` in the
  demo).

## qubix

- One `int 0x80`: positive numbers are process calls (`write`=4,
  `wait4`=7, `ioctl`=54 …), negative ones are aether ports (`AE_*`, for example
  `ae_msg_trap`=-31), `kill`=37, `steelctl`=500, `SYS_gantry`=600,
  `SYS_crypto`=601.
- `proc` is a wrapper over `task`: pid, fd table, `wait4`, zombies. `kill(pid)`
  sets the `killed` flag; the process terminates (status 137) on the nearest
  system call; a sleeper waiting without a timeout wakes only after an event.
  pids 0 and 1 are untouchable.
- tty has a raw mode (`ioctl`), on which the shell line editor is built.
- Programs (`qubix/progs.c`, `shell.c`) use only `libq.h` — that is, syscalls;
  when ring 3 arrives, they move without rewriting.
- `sysctlbyname` exposes text: `kern.*` (including `kern.cmdline`), `hw.*`,
  `hix.ps`, `hix.devtree`, `hix.dmesg`, `hix.boot`, `hw.display`, `hix.desktop`,
  `hix.crypto`.
- `hixfetch` (`lib/hixfetch.c`) — logo and system information, shared by the
  shell and Term.

## steel

`steel_device` → `steel_surface` → `steel_cmdbuf` (write commands — `commit`) →
`steel_layer` + compositor → console with ANSI colors and scrolling.

Two backends: VGA text and **pixel** (`gfxdev`/`pixdev`). The `bga` driver
finds a Bochs VBE adapter over PCI, sets 1024x768x32, takes the framebuffer from
BAR0 (it falls into the existing uncached 4 MB pages). `pixdev` draws 80x25
cells with a smooth 12x28 font and substitutes canvases of registered clients
for "holes" (`STEEL_HOLE`). `canvas`/`gfx.h` is an ARGB canvas: fill, rounded
rectangles, text, alpha blitting. The mouse cursor is drawn by the kernel with
background saving; programs do not need cursor code.

**Frame and smoothness.** `pixdev` draws into a clean screen copy in normal
memory (`fb`) and writes only the changed row segments to video memory
(`flush`): reading and alpha blending directly in video memory are very slow.
A window dragged with the mouse is shown as a **sprite** (`pixdev_float`,
`STEEL_CTL_FLOAT`): it follows the cursor pixel by pixel with a soft shadow,
and the background under it is not redrawn; on release the window snaps to the
nearest cells. The cursor does not save the background under itself; it is
composited on top on every `flush`.

**Panels** (`steel/panel.cpp`, `include/steel/panel.h`): a 960x700 grid sits in
a 1024x768 screen at y=24, so there is a 24 px strip above and 44 px below.
These are panels, and they are not made of cells: desk describes them with a
`steel_panel` structure (menus, clock, window buttons), `STEEL_CTL_PANEL` passes
it to the kernel, `pixdev_panel` redraws the panels only if the structure
changed. `panel_hit` reports what is under a pixel. The wallpaper is procedural
(`wall_px`): a starry sky with the Milky Way and mesa silhouettes.

## desk

The desktop is a qubix process (`apps/`, `include/desk/`). Keyboard and mouse
events (`qubix/input.h`) arrive through `steelctl(STEEL_CTL_EVENT)`, and a
finished frame goes through `STEEL_CTL_PRESENT`; while desk holds the screen,
the console is suspended. Programs are a `struct app` table
(`apps/desk_apps.c`); each window has its own surface and 8 KB of private
memory, so no allocator is needed. A pixel program (Chess) owns a canvas and
registers it with steel; mouse events arrive with pixel precision. The backend
(`struct desk_backend`: events, presentation, time, panels, power) is replaced
in tests by an event script. The global **Apps** and **Computer** menus on the
top panel (F9 from the keyboard) open as ordinary cell drop-down menus; the
window button on the bottom panel raises, minimizes or restores a window.

## Gantry

The common name of the platform above all kernels (in role, like Darwin over
XNU; the driver model is like IOKit). It is written in C++ (`gantry/`,
`include/gantry/gantry.h`); the pure C kernel knows nothing about it:
`kernel/main.c` calls `gantry_boot()` once.

- **Registry** — a tree of `Node` (name, class, up to 8 properties, children):
  Root → hix, drivers (one node per C driver), pci0 (bus devices).
  `sysctl hix.registry`, shell command `registry`.
- **Drivers** — `Driver` classes (`probe` gives a score, `start` starts);
  `GANTRY_DRIVER(Class, "ProviderClass")` registers a factory. For each node
  the driver with the best score wins and becomes its child node. Built in are
  `GenericPCI` (fallback, determines the device type) and `BochsDisplay`
  (1234:1111).
- So far this is description and matching: the C drivers themselves
  (`drivers/`) work as before and move under Gantry gradually. Kit — the SDK
  for user programs — will be built on the same classes.

## Rust in the kernel (optional)

`rust/hix-core` is a `no_std` staticlib for `i686-unknown-linux-gnu`, built with
stable rustc and linked into `build/hix.elf`. It does not call Linux and does
not allocate; the `hix_rust_*` ABI accepts only buffers already copied by the
kernel. The module currently implements ChaCha20-Poly1305 (RFC 8439).
`SYS_crypto` (601) is the only entry point; `qubix/crypto.c` validates ring 3
pointers, copies data into the kernel and wipes keys. Without i686-core the
kernel still builds, but the call returns `-ENOSYS`. Details and how to replace
the cipher are in [docs/CRYPTO.md](CRYPTO.md).

## Languages

hix, the aether kernel and drivers are C. For the layers above there is C++
without a runtime (`lib/cxxrt.c`, `lib/new.cpp`, `.init_array` section in
`linker.ld`). Rust is optional and lives behind the C ABI; Python is a host tool
(generators, vectors in `scripts/crypto_check.py`) and does not enter ring 0.

## Next (after 1.0.0)

Disk filesystem and installation to disk, SMP, a safe window API for ring 3,
the framebuffer from GRUB as a fallback for pixel mode, and a layer compositor
instead of the cell grid. Ring 3, separate address spaces and the ELF loader
already work — see the next section.

## Ring 3 (0.5.0, stage 1)

The user address space (`struct uspace`) is its own page directory. Kernel
entries (identity map of RAM, device windows) are copied from the kernel
directory and point to the same page tables, without the U bit. The private
window is `USER_BASE..USER_TOP` (0x40000000–0x80000000), above RAM, so it does
not overlap the identity map. Loading: `elf_parse` → `uspace_load_elf` (segments,
then an 8-page stack). The process starts as a normal kernel thread that in
`uspace_exec` turns itself into a user one (`enter_user`, `iret`). Return is only
through a trap on the kernel stack from `TSS.esp0`; `schedule()` calls
`arch_thread_switch`, which sets esp0 and CR3 for the next thread.

Security: a page fault or #GP with `cs & 3 == 3` goes to `arch_user_fault`
(the process terminates); for ring 3, `user_ok` checks only pages with the U bit
in its own space, taking write permission into account; a syscall whitelist.
`SYS_crypto` (601) is allowed and validates every pointer before copying.
Verified on the host (ELF parsing, page table construction) and by booting in
QEMU.

## Gantry: services and Kit (0.6.0)

Gantry is now more than a device registry. `Service` is a registry node with a
`call(op, msg)` method; `Services` keeps a table (16 services). `Notifier` sends
events to kernel subscribers and counts them by topic; `Properties` is a flat
key/value tree. Built-in Kits are `kit.system`, `kit.prop`, `kit.notify`. A
ring 3 program calls them through `SYS_gantry`; the kernel copies `kit_msg`,
validates pointers against the process address space and allows writes only
under `user.`. The program image for `/bin` is built by `scripts/mkapps.py` and
unpacked by `qubix/initfs.c`. More in SDK.md.
