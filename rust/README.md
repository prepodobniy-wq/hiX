# Rust в ядре hix

`rust/hix-core` — необязательный модуль ядра на Rust. Он собирается как
`staticlib` для `i686-unknown-linux-gnu`, линкуется в `build/hix.elf` и
вызывает только C ABI `hix_rust_*`; никаких функций Linux не использует.
Сейчас в нём ChaCha20-Poly1305 (RFC 8439) — рабочий пример для проверки
шифра, который принесёт соседняя команда.

Подробности, ABI и порядок замены алгоритма: [../docs/CRYPTO.md](../docs/CRYPTO.md).

## Состав

| Файл | Что |
|---|---|
| `src/lib.rs` | C ABI, проверка указателей, panic handler, unit-тесты |
| `src/chacha20.rs` | ChaCha20: блок, keystream, XOR, вектор RFC 8439 |
| `src/poly1305.rs` | Poly1305: 130-битная арифметика на `u32`, pad16 |
| `src/aead.rs` | AEAD: MAC, seal/open, очистка при ошибке, self-test |

## Сборка

Обычный `make` сам подхватывает Rust, если найден `core` для i686:

    make                 # ядро + ISO, Rust включается автоматически
    make rust            # только build/rust/libhix_rust.a
    make rust-test       # host-тесты crate (x86_64, cargo)
    make crypto-test     # векторы Python против C ABI (нужен cryptography)
    make rust-sysroot    # Debian/Ubuntu: скачать i386 core в .rust-sysroot

Если i686-core нет, ядро всё равно собирается: `SYS_crypto` возвращает
`-ENOSYS`, а `sysctl hix.crypto` пишет `unavailable`. Принудительно:

    make RUST=0
    make RUST=1 RUST_SYSROOT=/path/to/sysroot

`RUST=1` делает отсутствие тулчейна ошибкой. Готовый sysroot можно получить
через `rustup target add i686-unknown-linux-gnu` (тогда `RUST_SYSROOT` не
нужен) или `scripts/get-rust-i386.sh`.

## Ограничения ядра

- `no_std`, без аллокатора: только стеки и переданные буферы;
- обычный путь шифрования не использует SSE/MMX/x87: ядро не сохраняет их
  состояние при переключении потоков. В предкомпилированном `core` остаётся
  SSE-код форматирования паники; он исполняется только при фатальной панике,
  когда ядро всё равно останавливается;
- `panic=abort`: паника зовёт `hix_rust_panic()` и уходит в `panic()` ядра;
- статическая модель релокаций, без PIC/PIE;
- ключи и промежуточные величины затираются `write_volatile`;
- MAC сравнивается за постоянное время, но реализация не заявлена как
  защищённая от всех side-channel атак.

## Добавить модуль

1. Заведите `src/имя.rs`, добавьте `mod имя;` в `lib.rs`.
2. Экспортируйте `#[no_mangle] pub extern "C" fn hix_rust_имя(...)`.
3. Опишите запрос в `include/hix/cryptoabi.h` и обёртку в `qubix/crypto.c`.
4. Добавьте unit-тест в crate и вектор в `scripts/crypto_check.py`.
5. Проверьте `make rust-test`, `make crypto-test`, `make check`, затем
   загрузку в QEMU: `crypto`, `sysctl hix.crypto`, `cryptotest`.

Не меняйте сигнатуры существующих функций без обновления `HIX_CRYPTO_ABI`:
это версия ABI, которую проверяют и ядро, и host-тесты.
