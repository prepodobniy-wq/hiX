# SDK and Kit (1.0.0)

Ring 3 programs are written in C without libc. Everything needed is in
`user/sdk/`:

- `hix.h` — system calls (`hx_write`, `hx_read`, `hx_getpid`, `hx_exit`),
  strings, `hx_printf` (`%s %c %d %u %x %%`, width, `0` and `-` flags) and Kit
  calls;
- `libhix.c` — the implementation, `crt0.c` — the entry point (`_start` calls
  `main(void)`, the return code becomes the exit status).

## Building

Put `name.c` in `user/apps/` and run `make`. `scripts/mkapps.py` builds each
program into a 32-bit ELF at address 0x40000000 (`-ffreestanding -N`), packs
them into an initfs archive and embeds it in the kernel. At boot the archive is
unpacked into `/bin`; a program can be started from the shell by name
(`kitinfo`, `primes`). The shell launches it as a normal process that switches
to ring 3.

## Kit

Kit is a Gantry service with a `call(op, msg)` method. A program finds a service
by name (`kit_lookup`) and calls it through the single `SYS_gantry` system call
(600). The message is `struct kit_msg` (length and 120 bytes of data); the
kernel copies it and validates pointers against the process address space.

| Kit | What it provides |
|---|---|
| `kit.system` | system string, ticks (100 Hz), registry node count |
| `kit.prop` | property tree; everything is readable, only `user.*` keys are writable |
| `kit.notify` | event topic counters; only `user.*` can be published |

The kernel and drivers use the same Gantry in C++: `Services::publish`,
`Notifier` (kernel subscribers, events `proc.spawn`, `proc.exit`,
`gantry.kits`), `Properties`.

## Ring 3 system calls

Allowed: `exit`, `read`, `write`, `open`, `close`, `lseek`, `mkdir`, `unlink`,
`getdirentries`, `gettimeofday`, `wait4`, `posix_spawn`, `kill`,
`sysctlbyname`, sleep and `yield`, as well as `SYS_gantry` and `SYS_crypto`
(601). Every pointer goes through `user_ok`/`copy_str` against the process
address space. Closed: graphics (`steelctl`), IPC ports, `ioctl`, `reboot`.
In libhix they correspond to `hx_open`, `hx_readdir`, `hx_spawn`, `hx_wait`,
`hx_sleep_ms`, `hx_time`, `hx_sysctl`, `hx_read_file`, `hx_write_file` and
others.

`main(int argc, char **argv)`: arguments are placed on the stack (up to 16,
1 KiB). Examples: `lsbin [directory]`, `fstest` (a file in /tmp, a child
process, time).

## Cryptography (SYS_crypto)

`include/hix/cryptoabi.h` is the shared ABI for ring 3 and the kernel. In the
SDK:

    struct hix_crypto_req req = { .abi = HIX_CRYPTO_ABI, ... };
    long r = hx_crypto(HIX_CRYPTO_AEAD_SEAL, &req);

`hx_crypto()` is defined in `user/sdk/hix.h`; the operation and request fields
are described in [docs/CRYPTO.md](CRYPTO.md). The key (32 bytes), nonce (12)
and tag (16) are passed every time; the kernel copies the buffers, validates
pointers and wipes the keys after the call. Errors: `-EINVAL`, `-EFAULT`,
`-ENOMEM`, `-ENOSYS` (kernel without Rust), `-EBADMSG` (bad tag). An example is
`user/apps/cryptotest.c`; it is also a template for verifying another team's
cipher.

## Limitations (honest)

- No access rights or users: any program can create and delete files and
  toggle tweaks.
- There is no graphical application outside the kernel yet: a safe window
  surface call is needed.
- Everything is verified on the host (building with a real compiler, ELF
  parsing, argv layout, initfs unpacking, Gantry tests); in QEMU the smoke test
  runs `kitinfo`, `lsbin`, `fstest` and `cryptotest`. The full set of desktop
  scenarios is not run automatically.
