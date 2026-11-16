# Снимки экрана

Кадры соответствуют 1.0.0, используются в README и входят в пакет релиза.

| Файл | Откуда |
|---|---|
| `splash.png` | QEMU, `splash` из `scripts/smoke.py` (VGA-текст 720x400) |
| `shell.png` | QEMU, шелл после `hixfetch` (VGA-текст 720x400) |
| `screensaver-clock.png` | QEMU, `screensaver clock` (VGA-текст 720x400) |
| `desktop.png` | точный кадр `tests/test_desk` (1024x768x32) |
| `chess.png` | точный кадр `tests/test_desk`, шахматы (1024x768x32) |

Обновить после изменения интерфейса:

    make
    python3 scripts/smoke.py --require-rust   # splash.ppm и shell.ppm
    make shots                                # кадры десктопа (нужен Pillow)
    convert build/smoke/shots/splash.ppm docs/screenshots/splash.png
    convert build/smoke/shots/shell.ppm docs/screenshots/shell.png

Для конвертации PPM в PNG нужен ImageMagick (`convert`); `make shots` сам
использует Pillow, поэтому для одних только кадров десктопа можно запустить
`tests/build/test_desk` с `DESK_SHOTS=<каталог>`. Кадр
`screensaver-clock.png` снимается вручную той же техникой через QEMU-монитор
(`screendump`).

`make shots-check` сверяет пиксельные кадры с `tools/shotsum/golden.txt`;
если внешний вид изменился намеренно, обновите эталон через
`make shots-golden`.
