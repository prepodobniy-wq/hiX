# Boot, configuration and screensavers

hix no longer dumps the entire boot log onto the screen. By default it first
shows a splash screen with the logo, a live UTC clock, memory and crypto module
status, and then the normal shell. All of this is configured from the host,
without editing C code.

## Quick start

    scripts/hixctl show                    # current configuration
    scripts/hixctl splash on               # enable the splash screen
    scripts/hixctl splash seconds 4        # keep it for 4 seconds
    scripts/hixctl quiet on                # do not print info to VGA (dmesg keeps it)
    scripts/hixctl screensaver matrix      # choose a screensaver
    scripts/hixctl screensaver timeout 300 # 5 minutes idle
    scripts/hixctl build                   # same as make
    scripts/hixctl run --serial            # QEMU + COM1 in the terminal

`scripts/hixctl` is an ordinary bash script, the single configuration entry
point. It validates values and generates `build/hixconfig.h`, which is compiled
into the kernel. Editing `config/hix.conf` by hand also works:

    make config        # show the file
    make               # regenerate build/hixconfig.h and build
    scripts/hixctl check

`make` and `scripts/hixctl build` are equivalent: there is no separate
generator to run.

## config/hix.conf

| Key | Values | Meaning |
|---|---|---|
| `splash` | `on` / `off` | show the splash screen |
| `splash_seconds` | number | how many seconds it stays (any key skips) |
| `quiet` | `on` / `off` | remove info/notice from the VGA console |
| `screensaver` | `stars` `matrix` `clock` `blank` | what to run after idle |
| `screensaver_timeout` | number | idle seconds; `0` disables autostart |

`quiet = on` does not lose messages: they remain in the `dmesg` ring buffer
and go in full to COM1. Only warnings and errors reach VGA (levels 0–4), so
nothing "jumps" after the splash screen.

## Per-boot parameters

Values from `config/hix.conf` are compiled into the kernel, but the bootloader
can override them with an `hix.*` string — without rebuilding:

| Parameter | Values | Meaning |
|---|---|---|
| `hix.splash` | `0`/`off`, `1`/`on`, `N` | disable, enable, or keep for N seconds |
| `hix.quiet` | `0`/`off`, `1`/`on` | filter info/notice on VGA |
| `hix.screensaver` | `stars` `matrix` `clock` `blank`, `off` | choose or disable |
| `hix.screensaver_timeout` | number | idle in seconds; `hix.timeout` is shorter |
| `hix.safe` | `1`/`on` | no splash, no quiet, no screensaver |

Examples:

    qemu-system-i386 -m 128M -kernel build/hix.elf \
        -append 'hix.splash=4 hix.quiet=0 hix.screensaver=matrix hix.timeout=60'

    qemu-system-i386 -m 128M -kernel build/hix.elf -append 'hix.safe=1'

`hix.safe=1` is a quick way to check whether splash, VGA filtering or timers
are interfering with boot. Unknown keys and garbage values are ignored: the
command line also belongs to the bootloader. You can inspect the accepted
values like this:

    sysctl hix.boot      # compiled values + cmdline
    sysctl kern.cmdline  # bootloader string only
    dmesg | head         # boot config line at startup

## Serial console (COM1)

COM1 always works: the kernel mirrors `printk` there, and the tty mirrors shell
input and output. So `make run-serial` is not just a log but a full console:
you can type commands, run `desk`, `screensaver` and `halt`. GRUB has a
separate `serial console` entry for this:

    serial --unit=0 --speed=115200
    terminal_input serial
    terminal_output serial
    multiboot /boot/hix.elf hix.splash=0 hix.quiet=0

For `-kernel`, `-serial stdio` is enough (or `-serial unix:...` for scripts).
`scripts/smoke.py` uses exactly this path: it connects to COM1 through a Unix
socket and verifies boot without a window.

## Splash screen

`qubix/splash.c` is a normal qubix program (like `sh` or `desk`): it draws
through fd 1 and reads the keyboard through `libq`; the kernel does not notice
it. What you see:

- a wide hix block logo (CP437 half-blocks), version and tagline;
- a large UTC clock (5 rows of block digits), date and weekday;
- a status line: RAM, Rust/ChaCha20-Poly1305 availability;
- a progress bar and a stage label;
- `press any key to skip`.

`init` (pid 1) starts `splash` once before the first `sh`. If
`splash = off` or `splash_seconds = 0`, the program exits immediately. After
the splash screen the shell prints a compact header instead of the large logo.
You can see the screen again with the `splash` command (or `splash 4` to set
the duration in seconds).

The clock comes from the CMOS RTC via `gettimeofday()`; time is always UTC, as
in the `date` command.

## Screensavers

    screensaver            # from the configuration, usually stars
    screensaver stars
    screensaver matrix
    screensaver clock
    screensaver blank

Any key returns to the shell. After `screensaver_timeout` seconds without
input, the shell starts the selected screensaver itself; a line that was typed
but not submitted is not lost — after waking up, the prompt is redrawn together
with it. `screensaver_timeout = 0` disables autostart.

| Name | What it draws |
|---|---|
| `stars` | falling star field, three brightness levels |
| `matrix` | green rain of glyphs with a fading tail |
| `clock` | large UTC clock, date and uptime |
| `blank` | black screen |

Screensavers are also implemented as qprog programs (`qubix/screensaver.c`),
not as part of the kernel. Therefore they can be started manually from the
shell, and they do not interfere with `printk`: kernel messages still go to
`dmesg`, while the VGA console is simply not redrawn during `quiet`.

## Adding your own screensaver

1. Write a drawing function in `qubix/screensaver.c` that loops until
   `key_now()` returns true.
2. Add the name to the `screensaver_main()` parser and to the
   `scripts/hixctl` check (`valid_value`) — then the value can be set from the
   config.
3. Optionally add a line to the shell `help`.

No new system call is needed: the program can already read the keyboard in
raw mode with a timeout.

## Build and dependencies

`build/hixconfig.h` is included through `include/hix/bootconfig.h`. If the
file is missing (host tests, standalone tools), the old values apply: splash
off, quiet off, timeout 0. This allows building `tests/` without the kernel
Makefile wrapper.

When the configuration changes, only the files that include `bootconfig.h`
are rebuilt (`splash.c`, `screensaver.c`, `shell.c`, `console.c`): the normal
dependency comes from GCC `.d` files, and the header itself is created by an
order-only rule. A full rebuild is not needed.

## Limitations

- There is no guest bash: qubix uses its own shell. `scripts/hixctl` is the
  host bash layer; defaults are compiled into the kernel, and `hix.*` on the
  boot command line apply to one boot.
- VGA text has no "busy" cursor and no arbitrary graphics: only 80x25,
  16 colors and CP437. The logo uses CP437 half-blocks rather than UTF-8, so it
  looks the same in VGA text and in the pixel console.
- `quiet` affects only VGA. COM1 and `dmesg` remain detailed — this is
  intentional for debugging; COM1 also accepts input.
