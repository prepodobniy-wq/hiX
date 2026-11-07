# Стиль кода

Код следует Linux kernel coding style (`Documentation/process/coding-style.rst`):

- отступы — табы (ширина 8), фигурные скобки K&R, функции — с новой строки;
- имена `snake_case`, без typedef для структур, комментарии `/* */` (в `.c` допустим `//` в шапке);
- 80 колонок желательно, 100 жёсткий предел (как в checkpatch);
- первая строка файла — `SPDX-License-Identifier: GPL-2.0-only`
  (замените одной командой `sed`, если нужна другая лицензия);
- ошибки — `-E*` (нумерация Linux), `list_head`, `container_of`, `printk` с уровнями `KERN_*`;
- `scripts/check-style.sh` проверяет механические правила, `.clang-format` и `.editorconfig` — в корне.

Сами интерфейсы Linux (syscalls, ABI) не копируются: у hix свои — положительные
номера для процессов, отрицательные для портов aether (см. ARCHITECTURE.md).
