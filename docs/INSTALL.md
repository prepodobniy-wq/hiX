# Установка и первый запуск

hix — ядро для i386. «Установка» здесь означает сборку `build/hix.elf` и
запуск его в QEMU напрямую (`-kernel`) или с загрузочного ISO через GRUB.
Ставить систему на диск пока не нужно: дисковой ФС и установщика нет.

## Что нужно на хосте

Минимальный набор (Debian/Ubuntu):

    sudo apt-get install build-essential gcc-multilib g++-multilib nasm \
        python3 qemu-system-x86 grub-pc-bin grub-common xorriso

Fedora:

    sudo dnf install gcc gcc-c++ glibc-devel.i686 nasm python3 \
        qemu-system-x86 grub2-tools-extra xorriso

Arch:

    sudo pacman -S base-devel lib32-glibc nasm python3 qemu grub xorriso

Проверьте, что компилятор умеет `-m32`:

    echo 'int main(void) { return 0; }' | gcc -m32 -x c - -o /tmp/m32 && /tmp/m32
    nasm -v
    qemu-system-i386 --version

Для ISO нужен `grub-mkrescue` (пакет `grub-pc-bin` / `grub2-tools-extra`) и
`xorriso`; для `make smoke` — QEMU. Без `grub-mkrescue` обычный `make` всё
равно соберёт ELF и честно скажет, что ISO пропущен.

### Rust (необязательно для обычной сборки)

Rust-криптомодуль (`rust/hix-core`, ChaCha20-Poly1305) — единственная
необязательная зависимость. Обычный `make` включает его, если найден `core`
под `i686-unknown-linux-gnu`; иначе ядро собирается без него, а `SYS_crypto`
возвращает `-ENOSYS`.

Вариант с rustup:

    rustup target add i686-unknown-linux-gnu

Вариант для Debian/Ubuntu без root (скачивает `libstd-rust-dev:i386` в
`.rust-sysroot/`):

    scripts/hixctl doctor          # покажет, чего не хватает
    make rust-sysroot

Релизная сборка требует Rust явно: `make RUST=1` или `make release`.

## Сборка

    make                 # build/hix.elf + build/hix.iso (если есть GRUB)
    make strict          # чистка и сборка с -Werror
    make verify          # хост-тесты, стиль, Multiboot, Rust, crypto, кадры
    make smoke           # загрузка в QEMU и проверки через COM1
    make version         # версия из include/hix/version.h

Полезные переменные: `V=1` (показывать команды), `RUST=1|0|auto`,
`CROSS_COMPILE=i686-elf-`, `RUST_SYSROOT=/path/to/sysroot`.

## Запуск

    make run             # QEMU с -kernel, окно дисплея
    make run-serial      # то же, COM1 в текущем терминале
    make run-iso         # загрузка через GRUB с build/hix.iso
    make debug           # QEMU с gdb-стабом на :1234 и остановкой на старте

Внутри шелла:

    splash               # показать стартовый экран ещё раз
    screensaver matrix   # запустить скринсейвер вручную
    desk                 # рабочий стол
    hixfetch             # сводка о системе
    halt                 # выключить машину

## Загрузочное меню ISO

В `iso/boot/grub/grub.cfg` пять пунктов:

| Пункт | Что задаёт |
|---|---|
| hix | `hix.splash=on hix.quiet=on` |
| verbose boot | `hix.splash=on hix.quiet=0` |
| no splash, verbose | `hix.splash=0 hix.quiet=0` |
| safe mode | `hix.safe=1` (без splash, quiet и скринсейвера) |
| serial console | COM1 115200, весь ввод-вывод в терминал |

Те же параметры можно передать вручную: `-append 'hix.splash=0 hix.quiet=0'`
для `-kernel` или строкой `multiboot` в GRUB. Полный список — в
[docs/BOOT.md](BOOT.md).

## Настройка без пересборки

`config/hix.conf` и `scripts/hixctl` задают значения по умолчанию, которые
компилируются в ядро. Параметры `hix.*` в командной строке загрузчика
перекрывают их на один запуск:

    make run                       # значения из config/hix.conf
    qemu-system-i386 -m 128M -kernel build/hix.elf \
        -append 'hix.splash=4 hix.quiet=0 hix.screensaver=matrix hix.timeout=60'

`make` сам перегенерирует `build/hixconfig.h`; вручную запускать генератор не
нужно.

## Если не стартует

1. Соберите с полным выводом: `make clean && make V=1`, затем
   `make run-serial` и сохраните лог COM1.
2. Проверьте `make check` (стиль и Multiboot-заголовок) и `make test`.
3. Попробуйте safe mode:
   `qemu-system-i386 -m 128M -kernel build/hix.elf -append 'hix.safe=1'` или
   GRUB-пункт **safe mode** (без splash, quiet и скринсейвера). Если так
   работает, проблема в раннем VGA/таймерах.
4. Убедитесь, что QEMU запускается с `-vga std` (в `make run` это уже так);
   пиксельный режим требует Bochs VBE.
5. Пришлите `build/smoke/serial.log` или вывод `make run-serial`.

`make smoke` — самый быстрый способ проверить, что ядро вообще доходит до
шелла: он поднимает QEMU без окна, подключается к COM1 и прогоняет команды
`uname`, `crypto`, `kitinfo`, `fstest`, `cryptotest`, `hixfetch`,
скринсейвер и `halt`.

## Куда смотреть дальше

- [README.md](../README.md) — обзор проекта и возможности.
- [docs/BOOT.md](BOOT.md) — splash, quiet, скринсейверы, `hix.*`.
- [docs/ARCHITECTURE.md](ARCHITECTURE.md) — слои aether/qubix/steel/gantry.
- [docs/CRYPTO.md](CRYPTO.md) — Rust в ядре и ABI `SYS_crypto`.
- [docs/RELEASE.md](RELEASE.md) — как собирается и публикуется релиз.
