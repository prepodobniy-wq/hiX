# Writing code for hix: libraries and Tweaks

Three ways to add your own, from simple to deep.

## 1. Ring 3 program (user/apps, libhix)

A file `user/apps/name.c`, `main(argc, argv)`, `make` — the program is in
`/bin`. Safe: if the program crashes, the kernel survives. See SDK.md.

## 2. Tweak: a system extension in C++

A tweak connects to a running system through events, properties and services
and is enabled with `tweak on <name>` without a reboot. The file
`gantry/tweaks/my.cpp`:

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

Examples are in `gantry/tweaks/procwatch.cpp`. What is available inside:

| What | Where | Why |
|---|---|---|
| `Notifier::subscribe/post` | `gantry/kit.h` | events (`proc.spawn`, `proc.exit`, `tweak.on`, custom topics) |
| `Properties::set/get` | `gantry/kit.h` | shared property tree, visible from ring 3 via `kit.prop` |
| `Services::publish` + `Service::call` | `gantry/kit.h` | your own callable service (Kit) |
| `Driver` + `GANTRY_DRIVER` | `gantry/gantry.h` | driver with score-based matching |
| `Registry` / `Node` | `gantry/gantry.h` | device and service tree |
| `kmalloc/kfree`, `list.h`, `spinlock.h`, `printk` | `hix/*`, `aether/*` | standard kernel facilities |

Rules: no exceptions or RTTI; an event handler may run in the context of any
thread, do not block in it; `new`/`delete` work (`lib/new.cpp`).

## 3. Rust kernel module (rust/hix-core)

A `no_std` staticlib for i686, C ABI `hix_rust_*`. Suitable for algorithms and
small verified modules that need memory without libc. It currently contains
ChaCha20-Poly1305 behind `SYS_crypto`. Rules: no allocations, SSE/MMX/x87,
exceptions or Linux calls; a panic goes to the kernel `panic()`. The build is
optional (`make RUST=0`), details in [CRYPTO.md](CRYPTO.md); the full
`rust/README.md` is in the source archive.

## 4. Kernel (hix, aether, qubix, steel)

Pure C, Linux coding style, `make check` verifies the style. Add new modules
together with a host test in `tests/`.

## Limitations

Tweaks are compiled into the image: they are not loadable modules. Loadable
`.ko`-like extensions will require an ELF relocator in the kernel and a stable
ABI; that is on the roadmap.
