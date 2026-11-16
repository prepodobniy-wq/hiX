# Как готовится релиз

Релиз hix — это один тег `vX.Y.Z` и один набор файлов. Версия хранится
только в `include/hix/version.h`; ни README, ни тесты, ни Makefile не
содержат отдельной копии номера.

## Что делает `make release`

`scripts/release.sh` намеренно не трогает git и ничего не публикует. Он
собирает и проверяет ровно то, что попадёт в тег:

1. `make clean` — чистая сборка.
2. `make STRICT=1 RUST=1 all` — C/C++ с `-Werror`, Rust-криптомодуль
   обязателен, ISO обязателен.
3. `make verify` — `check` (стиль + Multiboot), `test` (хост-тесты),
   `rust-test`, `crypto-test`, `shots-check`.
4. `python3 scripts/smoke.py --require-rust` — загрузка в QEMU, работа
   через COM1: `dmesg`, splash, `uname`, `crypto`, `sysctl hix.crypto`,
   `progs`, `kitinfo`, `lsbin`, `fstest`, `cryptotest`, `hixfetch`,
   скринсейвер и `halt`.
5. Упаковка.

Проверка не «доверяет» предыдущей сборке: `make verify` заново прогоняет
хост-тесты и сверяет кадры, а smoke-тест грузит именно `build/hix.elf`.

## Результат

    dist/
      SHA256SUMS
      hix-1.0.0-src.tar.gz
      hix-1.0.0/
        SHA256SUMS
        hix.elf
        hix.iso
        README.md
        LICENSE
        BUILD-INFO
        build.log
        verify.log
        smoke.log
        smoke/serial.log
        smoke/shots/
        docs/
        config/hix.conf
        grub.cfg

`dist/SHA256SUMS` — суммы трёх публикуемых файлов (`hix.elf`, `hix.iso`,
`hix-1.0.0-src.tar.gz`) с теми именами, которые они получат на странице
релиза: скачайте их в один каталог и проверьте `sha256sum -c SHA256SUMS`.
Внутри `hix-1.0.0/SHA256SUMS` — суммы всего каталога с документацией, логами
и кадрами; проверяйте его из корня распакованного пакета.

`BUILD-INFO` содержит версию, дату, git-коммит (если это checkout), версии
компиляторов и размер ядра. Логи прикладываются, чтобы проверка была
воспроизводима.

## Перед запуском

    make rust-sysroot        # если ещё нет i686 core
    scripts/hixctl doctor    # gcc -m32, nasm, grub, qemu, rust
    make release

Скрипт упадёт, если нет `grub-mkrescue`, QEMU или i686 Rust: релиз не должен
молча оказаться без ISO или без криптомодуля. `DIST=/path` меняет каталог
вывода.

## Тег и публикация

Когда `dist/` проверен, создайте репозиторий и тег (если его ещё нет):

    git init
    git add .
    git commit -m "hix 1.0.0"
    git tag -a v1.0.0 -m "hix 1.0.0"
    git remote add origin git@github.com:OWNER/hix.git
    git push -u origin main
    git push origin v1.0.0

Затем опубликуйте файлы:

    gh release create v1.0.0 \
        dist/hix-1.0.0/hix.elf \
        dist/hix-1.0.0/hix.iso \
        dist/hix-1.0.0-src.tar.gz \
        dist/SHA256SUMS \
        --title "hix 1.0.0" \
        --notes-file docs/releases/1.0.0.md

Если GitHub CLI не настроен, приложите те же четыре файла вручную на
странице Releases. `dist/hix-1.0.0/` (документация и логи) можно приложить
целиком или оставить в артефактах CI — для пользователя важны ELF, ISO,
исходники и контрольные суммы.

## После публикации

1. Скачайте `hix.elf`, `hix.iso`, исходники и `SHA256SUMS` в чистый каталог
   и проверьте: `sha256sum -c SHA256SUMS`.
2. Загрузите ISO в QEMU: `qemu-system-i386 -m 128M -cdrom hix-1.0.0.iso`.
3. Проверьте `hix.elf` через `-kernel` и `make smoke` из архива исходников.
4. Убедитесь, что `BUILD-INFO` и `uname -a` показывают одну и ту же версию.

## Что не делает скрипт

- не создаёт и не двигает теги;
- не вызывает `gh`;
- не публикует в GitHub Packages;
- не собирает бинарники под другие платформы (hix — только i386).

Это сделано специально: сборка артефактов и публикация — разные решения.
