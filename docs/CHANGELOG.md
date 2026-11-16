# История версий

## 1.0.0 — первый публичный выпуск после 0.3.4
Включает локальные 0.3.5–0.6.1 и доводит загрузку, ABI ring 3 и релизную
инженерию до состояния, которое можно обещать пользователю. Подробные
заметки — [docs/releases/1.0.0.md](releases/1.0.0.md), схема версий —
[docs/VERSIONING.md](VERSIONING.md).

- **Стартовый экран, quiet boot, скринсейверы** (см. ниже): splash с широким
  блочным логотипом, часами UTC, датой, RAM, Rust-статусом и прогрессом;
  четыре скринсейвера; quiet-режим фильтрует только VGA info/notice, `dmesg`
  и COM1 остаются полными.
- **Хостовая настройка**: `config/hix.conf` → `build/hixconfig.h` через
  `scripts/hixctl`; `make config`, `make` перегенерирует заголовок.
- **Runtime-overrides**: `hix.splash=0|on|N`, `hix.quiet=0|1`,
  `hix.screensaver=NAME|off`, `hix.screensaver_timeout=N`/`hix.timeout=N`,
  `hix.safe=1`; разбор в `kernel/bootconfig.c`, sysctl `hix.boot` и
  `kern.cmdline`.
- **GRUB-меню**: обычный, verbose, без splash, safe mode и serial console
  на COM1 115200; COM1 — полноценный шелл (tty mirror в `qubix/tty.c`).
- **Одна версия**: `include/hix/version.h` — единственный источник;
  `HIX_RELEASE`/`HIX_EXPAND` в `include/hix/hix.h`; убраны старые
  `0.2.0`/`0.6.1` из кода, README, SDK и тестов.
- **Сборка и проверка**: `make version|verify|smoke|release`; `verify`
  прогоняет стиль, Multiboot, хост-тесты, Rust, Python-векторы и кадры;
  `release` делает чистую строгую сборку с Rust и ISO, `verify`, QEMU smoke
  и пакет `dist/hix-1.0.0/` с ELF/ISO/доками/логами, `BUILD-INFO` и
  `SHA256SUMS`. Скрипт — `scripts/release.sh`.
- **Исправления**: `GANTRY_LOOKUP` брал имя сервиса из `ubuf` вместо
  `arg` (`kitinfo: kit.system not found`); `hx_printf` не поддерживал
  `%-12s` и сдвигал аргументы (падение `lsbin`); smoke-тест получил
  корректную работу с serial/monitor, таймаутами и ранним возвратом
  скринсейвера; `parse_seconds` игнорирует мусор вместо подстановки
  максимума.
- **Документы**: `LICENSE` (GPL-2.0-only), `CONTRIBUTING.md`,
  `SECURITY.md`, `docs/INSTALL.md`, `docs/RELEASE.md`,
  `docs/VERSIONING.md`, `docs/releases/1.0.0.md`, CI в
  `.github/workflows/ci.yml`.
- Проверено: `make STRICT=1 RUST=1 all`, `make verify`,
  `python3 scripts/smoke.py --require-rust`; text около 218 КиБ с Rust.

## После 0.6.1 (вошло в 1.0.0) — стартовый экран, настройка и скринсейверы
- **Загрузка без потока логов.** `config/hix.conf` задаёт splash, его
  длительность, quiet-режим VGA, скринсейвер и таймаут. `scripts/hixctl` —
  bash-панель на хосте: `show`, `set`, `splash`, `quiet`, `screensaver`,
  `build`, `run`, `check`, `edit`, `reset`, `doctor`. `make` сам генерирует
  `build/hixconfig.h` через `include/hix/bootconfig.h`; хост-тесты без него
  получают старые значения по умолчанию.
- **Стартовый экран** (`qubix/splash.c`, `include/qubix/splash.h`): логотип,
  версия, большие часы UTC блочными цифрами, дата/день недели, RAM и состояние
  Rust-криптомодуля, прогресс-бар, пропуск любой клавишей. `init` запускает
  его один раз перед первым `sh`; шелл после этого печатает компактную шапку.
- **Quiet boot** (`steel/console.c`): при `quiet = on` VGA-консоль пропускает
  info/notice (уровни 5–6), предупреждения и ошибки остаются. `dmesg` и serial
  по-прежнему получают всё; `.max_level` steel-консоли задаётся при регистрации.
- **Скринсейверы** (`qubix/screensaver.c`): `stars`, `matrix`, `clock`, `blank` —
  обычные qprog-программы с raw-вводом и `TTY_SETTIMEOUT`; любая клавиша
  выходит. Шелл запускает выбранный после `screensaver_timeout` секунд простоя,
  сохраняя недопечатанную строку, и по команде `screensaver [NAME]`.
- QEMU: сняты кадры стартового экрана, matrix, stars, clock и blank;
  проверены пропуск клавишей, автозапуск по простою и возврат в шелл. Сборка:
  text 217 666 байт с Rust. Тесты: `make test`, `make check`,
  `make crypto-test`, `make rust-test`.
- Документация: [docs/BOOT.md](BOOT.md), обновлены README, ARCHITECTURE, ROADMAP.

## 0.6.1 — техническая часть: argv, системные вызовы, Tweaks
- Программы кольца 3 получают `main(argc, argv)` (раскладка на стеке System V,
  проверяется в `test_uspace`).
- Белый список вызовов кольца 3 расширен проверенными вызовами: open, lseek, mkdir,
  unlink, getdirentries, gettimeofday, wait4, posix_spawn, kill, sysctlbyname, sleep,
  yield. Закрыты graphics, ports, ioctl, reboot.
- libhix: файлы и каталоги, spawn/wait, sleep, time, sysctl, `hx_read_file`,
  `hx_write_file`; примеры `lsbin`, `fstest`.
- **Tweaks** (`include/gantry/tweak.h`, `gantry/tweak.cpp`, `gantry/tweaks/`): расширения
  ядра на C++, регистрируются `GANTRY_TWEAK()`, включаются и выключаются на ходу
  (`tweak`, `tweak on|off имя`, сервис `kit.tweak`, `sysctl hix.tweaks`). Примеры:
  `procwatch`, `procstats`.
- `docs/KERNEL-LIB.md`: как писать код под ядро (программа, tweak, ядро).
- **Rust в ядре** (`rust/hix-core`, необязательно): ChaCha20-Poly1305 по RFC 8439,
  `no_std`, только целочисленная арифметика, паника через `panic()` ядра.
  `SYS_crypto` (601) с ABI `include/hix/cryptoabi.h`; `qubix/crypto.c` проверяет
  указатели ring 3, копирует буферы и затирает ключи. Без i686-core ядро
  собирается, а вызов возвращает `-ENOSYS`. Линковка с `--gc-sections`, поэтому
  неиспользуемые функции `core` не попадают в образ.
- SDK: `hx_crypto()`, программа `cryptotest` (self-test, seal/open, подделка тега);
  шелл: `crypto`, `sysctl hix.crypto`; при загрузке — строка
  `qubix: Rust crypto up (ChaCha20-Poly1305)`.
- Python-эталон: `scripts/crypto_check.py` (`make crypto-test`) генерирует векторы
  через `cryptography` и сверяет их с тем же C ABI; 31 кейс. `make rust-test` —
  6 unit-тестов crate. Сборка: `make RUST=0|1`, `make rust-sysroot`.
- Проверено в QEMU: загрузка до планировщика, `crypto`, `sysctl hix.crypto`,
  `cryptotest: all checks passed`.
- Тесты: gantry 113, uspace 58.

## 0.6.0 — Gantry целиком: Kit, сервисы, SDK
- **Gantry** (`gantry/kit.cpp`, `include/gantry/kit.h`): таблица сервисов (`Service`,
  узел `Services` в реестре), центр уведомлений (`Notifier`, подписчики, счётчики
  тем), дерево свойств (`Properties`) и три встроенных Kit: `kit.system`, `kit.prop`,
  `kit.notify`. События ядра: `proc.spawn`, `proc.exit`, `gantry.kits`.
- `SYS_gantry` (600): поиск сервиса и вызов; ABI в `include/hix/kitabi.h`.
- **SDK** (`user/sdk`, `user/apps`, `scripts/mkapps.py`, `docs/SDK.md`): libhix, crt0,
  сборка программ на C в ELF для кольца 3, initfs в ядре → `/bin`, запуск из шелла по
  имени. Примеры: `kitinfo`, `primes`.
- Шелл запускает любую программу по имени (встроенную или из `/bin`).
- Тесты: gantry 81 проверка (сервисы, права на запись свойств, переполнения таблиц),
  `test_sdk` (настоящий компилятор → initfs → VFS → разбор ELF, испорченные архивы).
- Не сделано: графическое приложение вне ядра, аргументы argv в кольце 3, больше
  системных вызовов для кольца 3.

## 0.5.0 — исправления desk и шелла (этап 1)
- desk: нижняя строка убрана, остался только верхний док (меню, кнопки окон, часы).
- Окна можно сдвигать за левый и правый край экрана (остаются видимыми минимум
  4 клетки, чтобы окно можно было вернуть).
- Шелл: `exit` больше не вызывает бесконечный перезапуск (статический флаг
  `want_exit` не сбрасывался между запусками, из-за чего pid рос до 22000).
  `init` дополнительно ждёт 200 мс между перезапусками шелла.
- **Кольцо 3, этап 1** (`include/aether/uspace.h`, `aether/uspace*.c`, `lib/elf.c`,
  `qubix/uexec.c`, `user/*.asm`): у процесса свой каталог страниц; ядро в нём общее и
  только для supervisor, окно пользователя 0x40000000–0x7FFFFFFF приватное (бит U).
  ELF32-загрузчик проверяет все смещения и размеры; при переключении потоков меняются
  CR3 и TSS.esp0 (`arch_thread_switch`); вход через `iret` (`enter_user`); ошибка
  пользовательской программы убивает процесс (статус 139), а не ядро.
  Системные вызовы кольца 3: exit, read, write, close, getpid, getppid, остальное
  даёт -ENOSYS; указатели проверяются по адресному пространству процесса.
- Демо-программы на ассемблере, зашитые в образ: `hello3`, `fault3` (читает память
  ядра), `probe3` (плохой указатель, чужой вызов, `cli`). Встроенные программы
  qubix по-прежнему работают в кольце 0.
- Тесты: `test_uspace` (32-бит, 42 проверки): разбор ELF с испорченными заголовками,
  построение пространства, границы страниц, права записи, нехватка памяти без утечек.

## 0.4.0 — Gantry и плавное перетаскивание
- **Gantry** (`gantry/`, `include/gantry/gantry.h`, C++): реестр устройств, модель
  драйверов с подбором по оценке, встроенные драйверы GenericPCI и BochsDisplay,
  `gantry_boot()` из `kernel/main.c`, `sysctl hix.registry`/`hix.platform`, команда
  шелла `registry`, строка Platform в `hixfetch`. Ядро по-прежнему чистый C.
- Рендер: `pixdev` рисует в копию экрана в ОЗУ и переписывает в видеопамять только
  изменившиеся отрезки строк; курсор накладывается поверх (без сохранения фона).
- Окно при перетаскивании — пиксельный спрайт (`pixdev_float`, `STEEL_CTL_FLOAT`):
  двигается точно за курсором, а не клетками 12x28; тень, при отпускании окно
  встаёт в сетку. Для окон с пиксельными клиентами (Chess, Clock) прежний режим.
  Кадр перетаскивания окна 720x448 на хосте: около 0,4 мс против 1 мс при перерисовке
  клеток.
- Поле `class` в `struct hix_driver` переименовано в `kind` (слово `class` нельзя в C++).
- Тесты: gantry 40+, desk (спрайт совпадает с экраном побайтно, привязка к сетке).
- `docs/ROADMAP.md`: ring 3, Kit/SDK, Frutiger Aero.

## 0.3.6 — панели и звёздное небо (включает 0.3.5)
- Рабочий стол в духе GNOME 2, нарисованный пикселями в стиле Mac OS 9: верхняя
  панель (логотип hix, меню Apps и Computer, часы) и нижняя панель с кнопкой на
  каждое окно (иконка 24x24, активное окно «вдавлено», свёрнутое приглушено).
  Новые `steel/panel.cpp` (C++), `include/steel/panel.h`, `pixdev_panel`,
  `STEEL_CTL_PANEL`; сетка клеток сдвинута на y=24.
- Новые обои: процедурное звёздное небо, млечный путь, силуэты столовых гор.
- Шрифт `gfx_font_ubs` (12 px, жирный) для панелей; `scripts/genfont.py` обновлён.
- desk: глобальные меню Apps/Computer (мышью и по F9), клик по кнопке окна,
  Restart/Shut Down с подтверждением, Exit Desktop; backend получил `panel`,
  `wall_time`, `power`.
- Исправлено: выпадающее меню окон теряло текст пунктов (рамка перетирала роль
  клеток); состояние кнопок мыши переносилось между сеансами desk.
- Ядро: `kill(pid)` (флаг, завершение на следующем системном вызове; pid 0 и 1
  защищены), команда `kill` в шелле и в Term; sysctl `hw.display`, `hix.desktop`.
- `hixfetch` (`lib/hixfetch.c`): логотип и сведения о системе, как screenfetch;
  команда есть в шелле и в Term. Term уменьшен до 66x19, убраны упоминания чужих систем.
- Rust: `tools/shotsum` — контрольные суммы кадров десктопа
  (`make shots-golden`, `make shots-check`).
- Тесты: desk 92 проверки (панели, меню, кнопки окон, Restart/Shut Down, hixfetch, kill).

## 0.3.4 — тема Argent
- Клетка steel стала 32-битной: младшие 16 бит — как в VGA (символ + цвет), старшие
  — роль клетки (`STEEL_RCELL`, `steel_tag`, `steel_shade`). Текстовый дисплей роли
  игнорирует.
- Новая тема на C++ (`steel/theme.cpp`, `include/steel/theme.h`): обои, окна с
  полосатыми заголовками и кнопками закрытия/масштаба/сворачивания, фаски рамок,
  мягкие тени окон и меню, меню-бар, выпадающие меню, закруглённые кнопки,
  иконки 48x48 и подписи (с запоминанием текста под перекрывающими окнами).
- Палитра 16 цветов смягчена (`pixdev.c`).
- Иконки генерирует `scripts/genicons.py` (Python, Pillow) в `steel/icon_data.c`;
  `scripts/shots.py` / `make shots` рисуют все кадры десктопа в PNG и общий лист.
- Clock в пиксельном режиме: гладкий циферблат со стрелками (`gfx_line16`, линия
  с антиалиасингом), цифровой режим тоже в пикселях.
- `include/hix/list.h`: параметр `new` переименован (заголовок можно включать из C++).
- Тесты: gfx 67 (линия и тема), desk гоняется на пиксельном дисплее.

## 0.3.3 (включает 0.3.2)
- Исправлено зависание при запуске Clock: бесконечный цикл в алгоритме линии
  (Брезенхем) для стрелок под некоторыми углами. Добавлен тест, который
  рисует часы в 100+ моментов времени.
- Исправлен чёрный экран после выхода из desk: переключение адаптера в
  графику меняет регистры VGA и затирает шрифт текстового режима. Теперь
  шрифт сохраняется до входа в графику, а при выходе заново программируется
  режим 80x25 и шрифт возвращается (`vga_font_save`, `vga_text_restore`).
- BGA: задаётся виртуальная высота, равная видимой (уменьшает риск
  растягивания картинки в полноэкранном окне QEMU).
- C++ для слоёв над ядром: `CXX`-сборка (без исключений, RTTI и стандартной
  библиотеки), `lib/cxxrt.c`, `lib/new.cpp` (new/delete поверх kmalloc),
  конструкторы глобальных объектов через `.init_array`. Ядро hix остаётся на C.
  Первый модуль на C++ — шахматные правила (`apps/chess_rules.cpp`).
- Тесты: test_cxx (7), desk 64.

## 0.3.1
- Названия: убраны отсылки к чужим системам; слои — hix, aether, qubix, steel,
  desk; программы стола: Hub, Files, Notes, Term, Minefield, Bricks.
- steel: пиксельный режим 1024x768x32 (Bochs VBE через PCI), холсты с альфой,
  гладкие шрифты, пиксельный курсор, пиксельные клиенты поверх клеточной сетки.
- Chess: полноценные правила, движок с 4 уровнями, интерфейс с перетаскиванием,
  анимацией, часами, шкалой оценки и записью партии.
- shell: редактор строки, история, Tab, переменные, кавычки, `;`, `>`/`>>`,
  cwd и новые команды (cd, pwd, cp, mv, touch, wc, head, grep, top, free …).
- qubix: сырой режим tty и `ioctl`.
- Тесты: shell 111, gfx 55, chess 1602 (perft), desk 63.

## 0.3.0
- Рабочий стол с играми.

## 0.2.0
- Процессы, syscalls, VFS, порты и IPC, планировщик.

## 0.1.0
- hi-Kernel: загрузка, GDT/IDT/PIC, PMM, клавиатура, простой шелл.
