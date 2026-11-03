<div align="center">

# HIX
### hi is not Unix

![HIX screensaver](docs/assets/hixsaver.gif)

**An experimental operating system for i386**

</div>

# hix — hi is not unix

A hybrid kernel for i386, grown from your hi-Kernel 0.1.0. The kernel is divided into
layers with their own names, and the code is styled according to the Linux coding style. Version: **0.3.1**.

| Name | Role | Where |
|---|---|---|
| **hix** | entire kernel: booting, interrupts, printk, panic, drivers | `arch/` `kernel/` `drivers/` `lib/` |
| **aether** | tasks, threads, scheduler, ports and IPC, memory | `aether/` |
| **qubix** | processes, system calls, VFS, tty, shell | `qubix/` |
| **steel** | surfaces, canvases, compositor, console, pixel mode | `steel/` |
| **desk** | desktop and programs (qubix process) | `apps/` `include/desk/` |

## Build and Run

    make              # build/hix.elf (+ build/hix.iso, if grub-mkrescue is available)
    make run          # QEMU directly, without GRUB:  qemu-system-i386 -kernel
    make run-serial   # same + COM1 console in your terminal
    make run-iso      # via GRUB
    make test         # host tests (including chess engine with perft)
    make check        # style + multiboot header check
    make strict       # build with -Werror

Requirements: gcc (with -m32), nasm, ld; for ISO — grub-mkrescue + xorriso; for running — QEMU.

## Graphics (steel)

If the kernel detects a Bochs VBE adapter (QEMU with `-vga std`, which is the default;
also Bochs and VirtualBox), steel switches to **1024x768x32**:
smooth font, pixel mouse cursor, canvases with alpha blending, rounded
rectangles. On other hardware, it falls back to the standard 80x25 text mode —
everything still works there, just without pixel-based programs.

The desktop still speaks in 80x25 cells; "holes" in the cell grid
reveal the pixel canvases of programs, so windows overlap as usual.
Programs register a canvas via `steelctl(STEEL_CTL_CLIENT_SET)` and report
changes via `STEEL_CTL_CLIENT_DIRTY`.

## Desktop (desk)

In the shell, the `desk` command starts the desktop: Hub with Main and Games groups,
windows with menus, dragging, minimizing, double-clicking icons, Alt+Tab,
Alt+F4. A PS/2 mouse is required (available in QEMU); the keyboard always works.

| Program | What it is |
|---|---|
| Files, Notes | qubix-VFS files: view, edit, save |
| Calculator, Clock | calculator (left-to-right), clock via RTC |
| System Info, Task List | `sysctl` data and process list |
| Term | command line inside a window |
| Minefield, Reversi, Snake, Bricks | games; rules are separated from windows (`include/desk/games.h`) |
| **Chess** | chess in pixel mode, see below |

`desk chess` (or simply `chess` in the shell) opens the desk directly with chess.

### Chess

Engine (`apps/chess_rules.c`): full rules — castling, en passant,
promotion, checkmate, stalemate, threefold repetition, 50-move rule, insufficient
material; verified with `perft` counts. Player vs computer (4 levels:
alpha-beta with quiescence search) or two players on the same screen.

Interface: 8x8 board with last-move highlighting and possible-move dots,
dragging pieces with the mouse or click-click, piece selection on promotion,
move animation, evaluation bar, player cards with clocks, game recording in
algebraic notation, New Game / Undo / Flip buttons, Game / Play / Help menus.

## Shell

Line editing in place (arrows, Home/End, Ctrl-A/E/K/U/W/L), history
(↑/↓), Tab-completion for commands and paths, variables (`set X=1`, `$X`,
`$?`), quotes, `;`, redirection `>` and `>>`, current directory.

`help` · `ls [-l]` `cd` `pwd` `cat` `echo` `mkdir` `rm` `touch` `cp` `mv` `wc`
`head` `grep` · `ps` `top` `free` `mem` `devtree` `irqs` `dmesg` `sysctl` ·
`set` `unset` `history` · `run` `progs` `ipc` `desk` `gfx` · `date` `uptime`
`uname` `sleep` · `crash` `panic` `reboot` `halt` `exit`.

`kill` is not yet available: threads do not have cancellation.

## Important

The build has been verified on the host (tests, `-Werror`, ELF/multiboot parsing, pixel
output snapshots), but **has not been booted on real QEMU or hardware** —
the development environment did not have QEMU. If the kernel fails to boot,
please send the output of `make run-serial`.

Architecture: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), style:
[docs/CODING-STYLE.md](docs/CODING-STYLE.md), authors and fonts:
[docs/CREDITS.md](docs/CREDITS.md), history: [docs/CHANGELOG.md](docs/CHANGELOG.md).