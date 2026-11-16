# Криптография и Rust в ядре

В ядре hix есть необязательный модуль на Rust. Сейчас это
ChaCha20-Poly1305 (RFC 8439): он показывает, как подключить к ядру шифр,
написанный на другом языке, и как проверить его независимой реализацией на
Python. Модуль можно заменить шифром соседней команды, сохранив ABI.

## Почему Rust, а не Python, в ring 0

Python в кольце 0 не запускается: CPython — большое приложение с libc,
аллокатором и платформенным кодом, а ошибка в нём роняет всё ядро. Питон
здесь используется по назначению — как **эталон на хосте**: он генерирует
векторы, с которыми сравнивается реализация в ядре.

Если Python нужен гостю, правильный путь — отдельная программа ring 3
(например, MicroPython на голом железе или свой интерпретатор), а не модуль
ядра. Системный вызов `SYS_crypto` для этого уже есть: программа просит
ядро зашифровать/расшифровать свой буфер, не выполняя код в привилегированном
режиме.

## Где что лежит

| Файл | Роль |
|---|---|
| `rust/hix-core/src/chacha20.rs` | ChaCha20, только целочисленная арифметика |
| `rust/hix-core/src/poly1305.rs` | Poly1305 на `u32` |
| `rust/hix-core/src/aead.rs` | ChaCha20-Poly1305, MAC за постоянное время, self-test |
| `rust/hix-core/src/lib.rs` | C ABI `hix_rust_*`, panic handler, проверки указателей |
| `include/hix/cryptoabi.h` | общий ABI: `SYS_crypto`, `struct hix_crypto_req` |
| `qubix/crypto.c` | доверенная граница: проверка указателей, копии, затирание |
| `qubix/syscall.c` | диспетчер `SYS_crypto` (601), `sysctl hix.crypto` |
| `qubix/shell.c` | команда `crypto` |
| `user/sdk/hix.h` | `hx_crypto()` для ring 3 |
| `user/apps/cryptotest.c` | проверка seal/open и подделки тега из ring 3 |
| `scripts/crypto_check.py` | векторы Python → C ABI на хосте |
| `scripts/rust-*.sh` | поиск i686-core и сборка staticlib |

## Сборка

    make                         # Rust включается, если найден i686 core
    make RUST=0                  # ядро без Rust, SYS_crypto = -ENOSYS
    make RUST=1                  # требовать Rust, иначе ошибка
    make rust                    # только build/rust/libhix_rust.a
    make rust-test               # host-тесты crate через cargo
    make crypto-test             # Python-векторы против C ABI
    make rust-sysroot            # Debian/Ubuntu: i386 core в .rust-sysroot

Для сборки нужен стабильный Rust и `core` под `i686-unknown-linux-gnu`.
Подходят оба варианта:

    rustup target add i686-unknown-linux-gnu
    # или, без root, на Debian/Ubuntu:
    scripts/get-rust-i386.sh

На Debian/Ubuntu архитектура i386 должна быть включена (один раз):
`sudo dpkg --add-architecture i386 && sudo apt-get update`.

Затем `make` или `make RUST_SYSROOT=$PWD/.rust-sysroot`. Модуль собирается
командой `scripts/rust-build.sh` с `--cfg 'feature="kernel"'`,
`-C panic=abort`, `-C relocation-model=static` и
`-C target-feature=-sse,-sse2,-fxsr`. Ядро не сохраняет SSE-состояние при
переключении потоков, поэтому SIMD в модуле запрещён.

Без i686-core обычный `make` продолжает работать: `HIX_HAVE_RUST` не
определён, `sysctl hix.crypto` отвечает `unavailable`, а `SYS_crypto`
возвращает `-ENOSYS`. Это важно для пользователей, которым криптография не
нужна.

## ABI

Ring 3 и ядро видят один заголовок `include/hix/cryptoabi.h`:

    long r = syscall(SYS_crypto, op, &req);

| Поле | Смысл |
|---|---|
| `abi` | `HIX_CRYPTO_ABI`, сейчас 1 |
| `flags` | для `CHACHA20` — начальный 32-битный счётчик |
| `key`, `key_len` | ключ, ровно 32 байта |
| `nonce`, `nonce_len` | nonce, ровно 12 байт |
| `aad`, `aad_len` | дополнительные данные, до 4096 байт |
| `in`, `in_len` | вход, до 64 КиБ |
| `out`, `out_len` | выход; `out_len >= in_len` |
| `tag`, `tag_len` | тег, ровно 16 байт |
| `result` | число байт или 0 для self-test |

Операции:

| `op` | Что делает |
|---|---|
| `HIX_CRYPTO_SELFTEST` (0) | прогоняет векторы RFC 8439 |
| `HIX_CRYPTO_CHACHA20` (1) | XOR потока, `flags` = счётчик |
| `HIX_CRYPTO_AEAD_SEAL` (2) | шифрует и записывает тег |
| `HIX_CRYPTO_AEAD_OPEN` (3) | проверяет тег, затем расшифровывает |

Ошибки: `-EINVAL` (размеры/ABI/указатель), `-EFAULT` (чужой указатель),
`-ENOMEM` (нет памяти под копию), `-ENOSYS` (ядро без Rust), `-EBADMSG`
(тег не сошёлся). При ошибке открытия выходной буфер затирается.

### Граница доверия

`qubix/crypto.c` не передаёт пользовательские указатели в Rust. Сначала
`qubix_user_range_ok()` проверяет диапазон в адресном пространстве процесса
(отдельно чтение/запись), затем буферы копируются в память ядра. После
вызова ключ, nonce, тег и копии затираются. Размеры ограничены
`HIX_CRYPTO_AAD_MAX` и `HIX_CRYPTO_DATA_MAX`, чтобы запрос не мог занять
всю кучу.

Rust дополнительно проверяет `NULL` и длину; диапазоны указателей уже
проверены ядром, поэтому в модуль попадают только валидные буферы.

## Проверка

Три независимых уровня:

1. **Unit-тесты crate** — `make rust-test` (RFC 8439 и граничные размеры).
2. **Python-эталон** — `make crypto-test`. `scripts/crypto_check.py` берёт
   `cryptography`, генерирует случайные ключи/nonce/сообщения, собирает C
   программу, линкует её с тем же Rust C ABI и печатает число прошедших
   кейсов. Сид можно задать: `python3 scripts/crypto_check.py --seed 123`.
3. **Ядро в QEMU**:

       make run
       # в шелле:
       sysctl hix.crypto
       crypto
       cryptotest

   Ожидаемо: `Rust, ChaCha20-Poly1305, selftest ok`, затем
   `cryptotest: all checks passed`. При загрузке в serial-лог пишется
   `qubix: Rust crypto up (ChaCha20-Poly1305)`.

`cryptotest` — шаблон для чужого шифра: он проверяет self-test, seal/open и
то, что испорченный тег даёт `-EBADMSG`.

## Заменить шифр на шифр соседней команды

1. Оставьте `struct hix_crypto_req` и номера операций; при необходимости
   добавьте новые `op` и поля, поднимите `HIX_CRYPTO_ABI`.
2. Реализуйте алгоритм в `rust/hix-core/src/` (или подключите C-модуль и
   оставьте Rust обёртку). Сохраните сигнатуры `hix_rust_*` или добавьте
   новые и обновите `qubix/crypto.c`.
3. Добавьте self-test с опубликованным вектором и unit-тест.
4. Добавьте генерацию векторов в `scripts/crypto_check.py`; для чужого
   формата можно сгенерировать отдельный файл и сравнить побайтно.
5. Обновите `user/apps/cryptotest.c` под новый ABI и прогоните все три
   уровня проверки.

Если алгоритм уже есть на Python, самый быстрый путь — сначала получить
эталонные байты:

    from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
    ct = ChaCha20Poly1305(key).encrypt(nonce, plaintext, aad)
    ciphertext, tag = ct[:-16], ct[-16:]

Те же байты должны появиться из `cryptotest`/`crypto_check`; расхождение
печатается как первый неверный кейс.

## Ограничения

- Это учебный/прикладной модуль, а не аудированная криптобиблиотека.
- Нет аппаратного ускорения, AES-NI и т.п.; только переносимый целочисленный
  код. Обычный путь crypto SSE-free; SSE-код форматирования паники из
  предкомпилированного `core` исполняется только при фатальной панике.
- Нет управления ключами: ключ приходит из ring 3 на каждый вызов и
  затирается после. Постоянного хранилища секретов в ядре нет.
- Python в ring 0 не поддерживается; для гостя нужен ring 3 (MicroPython
  или отдельный интерпретатор).
- Модуль не проверялся на реальном железе; QEMU и host-тесты — это не
  аудит безопасности.
