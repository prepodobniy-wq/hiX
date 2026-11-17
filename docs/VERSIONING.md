# Versions and compatibility

## One version for everything

`include/hix/version.h` is the only source of the number:

    #define HIX_RELEASE "1.0.0"

From it are built `HIX_VERSION`/`HIX_RELEASE` in `include/hix/hix.h`,
the `uname` string, the shell header, `hixfetch`, `kitinfo` and the release
artifacts. `make version` prints the value. Do not add the number to code,
the Makefile or tests: a version mismatch is a release-engineering bug, not a
minor issue. In the README and release notes the number is allowed as part of
the release description, but check it against `make version` before tagging.

## Scheme

hix follows `MAJOR.MINOR.PATCH` in the spirit of semver, with a caveat: this is
a kernel and an operating environment, not a library.

| Part | When it increments | Example |
|---|---|---|
| `MAJOR` | the ring 3 ABI, initfs/ELF format, `SYS_*`, boot protocol, or behavior that programs rely on breaks | 1.0.0 → 2.0.0 |
| `MINOR` | new features without breaking existing ones: a new syscall, driver, program, boot parameter | 1.0.0 → 1.1.0 |
| `PATCH` | fixes and compatible improvements | 1.0.0 → 1.0.1 |

Before 1.0.0 the number lived its own life: `0.3.x` was the Argent theme and
panels, `0.4.0` Gantry, `0.5.0` ring 3, `0.6.x` Kit/SDK and Rust. The public
release on GitHub was `0.3.4`; local `0.3.5`–`0.6.1` were not published. That
is why `1.0.0` is the first public release after `0.3.4` and at the same time
a statement of stability, not "just another number".

## What 1.0 promises

- A normal `make` builds on a clean machine with GCC/NASM; Rust remains
  optional (`SYS_crypto = -ENOSYS` without it).
- `config/hix.conf` and `scripts/hixctl` remain the host-side configuration
  layer; `hix.*` parameters on the boot command line override them for a single
  boot.
- Ring 3 guest programs use the documented `SYS_*` and `libhix`; the
  `SYS_crypto` ABI is versioned by the `abi` field.
- `build/hix.elf` boots via Multiboot in QEMU and via GRUB from the ISO.
- `make verify` tests and the smoke test pass on a supported host.

## What it does not promise

- Installation to disk, ext2/FAT, SMP, users and access permissions.
- Compatibility with Linux binaries or POSIX applications.
- Data persistence across reboots: the VFS lives in memory.
- Stability of internal C kernel functions: `aether_*`, `qubix_*`, `steel_*`
  may change without notice. The ABI is only `include/hix/*abi.h` and the
  documented syscall numbers.

## How to ship a patch

1. Fix the code and add a test that catches the regression.
2. Update `HIX_RELEASE` (for example, `1.0.1`).
3. Add a section to `docs/CHANGELOG.md`.
4. `make release`; verify `dist/SHA256SUMS` and `BUILD-INFO`.
5. Tag `v1.0.1` and publish the four files as described in
   [docs/RELEASE.md](RELEASE.md).

For `MINOR`, also update `docs/ROADMAP.md` and, if ring 3 behavior changes,
`docs/SDK.md`.
