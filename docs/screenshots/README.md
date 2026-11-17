# Screenshots

The frames correspond to 1.0.0, are used in the README and are included in the
release package.

| File | Source |
|---|---|
| `splash.png` | QEMU, `splash` from `scripts/smoke.py` (VGA text 720x400) |
| `shell.png` | QEMU, the shell after `hixfetch` (VGA text 720x400) |
| `screensaver-clock.png` | QEMU, `screensaver clock` (VGA text 720x400) |
| `desktop.png` | exact frame from `tests/test_desk` (1024x768x32) |
| `chess.png` | exact frame from `tests/test_desk`, chess (1024x768x32) |

Refresh after changing the UI:

    make
    python3 scripts/smoke.py --require-rust   # splash.ppm and shell.ppm
    make shots                                # desktop frames (requires Pillow)
    convert build/smoke/shots/splash.ppm docs/screenshots/splash.png
    convert build/smoke/shots/shell.ppm docs/screenshots/shell.png

Converting PPM to PNG requires ImageMagick (`convert`); `make shots` uses
Pillow itself, so for desktop frames alone you can run
`tests/build/test_desk` with `DESK_SHOTS=<directory>`. The
`screensaver-clock.png` frame is captured manually with the same technique via
the QEMU monitor (`screendump`).

`make shots-check` compares the pixel frames with `tools/shotsum/golden.txt`;
if the appearance changed intentionally, refresh the baseline with
`make shots-golden`.
