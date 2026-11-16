# Писать код под hix: библиотеки и Tweaks

Три способа добавить своё, от простого к глубокому.

## 1. Программа кольца 3 (user/apps, libhix)

Файл `user/apps/имя.c`, `main(argc, argv)`, `make` — программа в `/bin`. Безопасно: упала
программа — ядро живо. См. SDK.md.

## 2. Tweak: расширение системы на C++

Tweak подключается к работающей системе через события, свойства и сервисы и включается
командой `tweak on <имя>` без перезагрузки. Файл `gantry/tweaks/мой.cpp`:

```cpp
#include <gantry/tweak.h>
using namespace gantry;

class Hello : public Tweak {
public:
	Hello() : Tweak("hello", "say hello when a process starts") {}
	int enable() override
	{
		id_ = Notifier::subscribe("proc.spawn", on, nullptr);
		return id_ < 0 ? id_ : 0;
	}
	void disable() override { Notifier::unsubscribe(id_); }
private:
	static void on(const char *topic, long pid, void *) { pr_info("hi, pid %ld\n", pid); }
	int id_ = -1;
};
GANTRY_TWEAK(Hello);
```

Примеры — `gantry/tweaks/procwatch.cpp`. Что доступно внутри:

| Что | Где | Зачем |
|---|---|---|
| `Notifier::subscribe/post` | `gantry/kit.h` | события (`proc.spawn`, `proc.exit`, `tweak.on`, свои темы) |
| `Properties::set/get` | `gantry/kit.h` | общее дерево свойств, видно из кольца 3 через `kit.prop` |
| `Services::publish` + `Service::call` | `gantry/kit.h` | свой вызываемый сервис (Kit) |
| `Driver` + `GANTRY_DRIVER` | `gantry/gantry.h` | драйвер с подбором по оценке |
| `Registry` / `Node` | `gantry/gantry.h` | дерево устройств и сервисов |
| `kmalloc/kfree`, `list.h`, `spinlock.h`, `printk` | `hix/*`, `aether/*` | обычные средства ядра |

Правила: без исключений и RTTI; обработчик события может выполняться в контексте
любого потока, не блокируйтесь в нём; `new`/`delete` работают (`lib/new.cpp`).

## 3. Rust-модуль ядра (rust/hix-core)

`no_std` staticlib для i686, C ABI `hix_rust_*`. Годится для алгоритмов и
небольших проверенных модулей, где нужна память без libc. Сейчас там
ChaCha20-Poly1305 за `SYS_crypto`. Правила: без аллокаций, SSE/MMX/x87,
исключений и Linux-вызовов; паника уходит в `panic()` ядра. Сборка
необязательна (`make RUST=0`), подробности — [CRYPTO.md](CRYPTO.md);
полный `rust/README.md` лежит в исходном архиве.

## 4. Ядро (hix, aether, qubix, steel)

Чистый C, Linux coding style, `make check` проверяет стиль. Новые модули добавляйте
вместе с хост-тестом в `tests/`.

## Ограничения

Tweaks вкомпилированы в образ: это не загружаемые модули. Загружаемые `.ko`-подобные
расширения потребуют ELF-релокатора в ядре и стабильного ABI; это в планах.
