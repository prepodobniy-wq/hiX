# Cryptography and Rust in the kernel

The hix kernel has an optional Rust module. It currently implements
ChaCha20-Poly1305 (RFC 8439): it shows how to connect a cipher written in
another language to the kernel, and how to verify it with an independent
Python implementation. The module can be replaced with another team's cipher
while keeping the ABI.

## Why Rust and not Python in ring 0

Python does not run in ring 0: CPython is a large application with libc, an
allocator and platform code, and a bug in it brings down the whole kernel.
Python is used here for its intended purpose — as a **reference on the host**:
it generates vectors against which the kernel implementation is compared.

If a guest needs Python, the right path is a separate ring 3 program (for
example, MicroPython on bare metal or a custom interpreter), not a kernel
module. The `SYS_crypto` system call already exists for this: a program asks
the kernel to encrypt/decrypt its buffer without executing code in privileged
mode.

## Where things live

| File | Role |
|---|---|
| `rust/hix-core/src/chacha20.rs` | ChaCha20, integer arithmetic only |
| `rust/hix-core/src/poly1305.rs` | Poly1305 on `u32` |
| `rust/hix-core/src/aead.rs` | ChaCha20-Poly1305, constant-time MAC, self-test |
| `rust/hix-core/src/lib.rs` | C ABI `hix_rust_*`, panic handler, pointer checks |
| `include/hix/cryptoabi.h` | shared ABI: `SYS_crypto`, `struct hix_crypto_req` |
| `qubix/crypto.c` | trust boundary: pointer validation, copies, wiping |
| `qubix/syscall.c` | `SYS_crypto` dispatcher (601), `sysctl hix.crypto` |
| `qubix/shell.c` | `crypto` command |
| `user/sdk/hix.h` | `hx_crypto()` for ring 3 |
| `user/apps/cryptotest.c` | seal/open and tag forgery check from ring 3 |
| `scripts/crypto_check.py` | Python vectors → C ABI on the host |
| `scripts/rust-*.sh` | locating i686-core and building the staticlib |

## Building

    make                         # Rust is enabled if an i686 core is found
    make RUST=0                  # kernel without Rust, SYS_crypto = -ENOSYS
    make RUST=1                  # require Rust, fail otherwise
    make rust                    # only build/rust/libhix_rust.a
    make rust-test               # host tests for the crate via cargo
    make crypto-test             # Python vectors against the C ABI
    make rust-sysroot            # Debian/Ubuntu: i386 core in .rust-sysroot

Building requires stable Rust and `core` for `i686-unknown-linux-gnu`. Both
options work:

    rustup target add i686-unknown-linux-gnu
    # or, without root, on Debian/Ubuntu:
    scripts/get-rust-i386.sh

On Debian/Ubuntu the i386 architecture must be enabled (once):
`sudo dpkg --add-architecture i386 && sudo apt-get update`.

Then `make` or `make RUST_SYSROOT=$PWD/.rust-sysroot`. The module is built by
`scripts/rust-build.sh` with `--cfg 'feature="kernel"'`, `-C panic=abort`,
`-C relocation-model=static` and `-C target-feature=-sse,-sse2,-fxsr`. The
kernel does not save SSE state when switching threads, so SIMD is forbidden in
the module.

Without i686-core, a normal `make` still works: `HIX_HAVE_RUST` is not
defined, `sysctl hix.crypto` reports `unavailable`, and `SYS_crypto` returns
`-ENOSYS`. This matters for users who do not need cryptography.

## ABI

Ring 3 and the kernel share one header `include/hix/cryptoabi.h`:

    long r = syscall(SYS_crypto, op, &req);

| Field | Meaning |
|---|---|
| `abi` | `HIX_CRYPTO_ABI`, currently 1 |
| `flags` | for `CHACHA20` — the initial 32-bit counter |
| `key`, `key_len` | key, exactly 32 bytes |
| `nonce`, `nonce_len` | nonce, exactly 12 bytes |
| `aad`, `aad_len` | additional data, up to 4096 bytes |
| `in`, `in_len` | input, up to 64 KiB |
| `out`, `out_len` | output; `out_len >= in_len` |
| `tag`, `tag_len` | tag, exactly 16 bytes |
| `result` | number of bytes, or 0 for a self-test |

Operations:

| `op` | What it does |
|---|---|
| `HIX_CRYPTO_SELFTEST` (0) | runs the RFC 8439 vectors |
| `HIX_CRYPTO_CHACHA20` (1) | XOR the stream, `flags` = counter |
| `HIX_CRYPTO_AEAD_SEAL` (2) | encrypt and write the tag |
| `HIX_CRYPTO_AEAD_OPEN` (3) | verify the tag, then decrypt |

Errors: `-EINVAL` (sizes/ABI/pointer), `-EFAULT` (foreign pointer),
`-ENOMEM` (no memory for a copy), `-ENOSYS` (kernel without Rust), `-EBADMSG`
(tag mismatch). On a failed open, the output buffer is wiped.

### Trust boundary

`qubix/crypto.c` never passes user pointers into Rust. First
`qubix_user_range_ok()` validates the range in the process address space
(separately for read/write), then the buffers are copied into kernel memory.
After the call, the key, nonce, tag and copies are wiped. Sizes are limited by
`HIX_CRYPTO_AAD_MAX` and `HIX_CRYPTO_DATA_MAX` so a request cannot consume the
entire heap.

Rust additionally checks `NULL` and lengths; the pointer ranges have already
been validated by the kernel, so only valid buffers reach the module.

## Verification

Three independent levels:

1. **Crate unit tests** — `make rust-test` (RFC 8439 and edge sizes).
2. **Python reference** — `make crypto-test`. `scripts/crypto_check.py` uses
   `cryptography`, generates random keys/nonces/messages, builds a C program,
   links it against the same Rust C ABI and prints the number of passing
   cases. The seed can be set: `python3 scripts/crypto_check.py --seed 123`.
3. **Kernel in QEMU**:

       make run
       # in the shell:
       sysctl hix.crypto
       crypto
       cryptotest

   Expected: `Rust, ChaCha20-Poly1305, selftest ok`, then
   `cryptotest: all checks passed`. At boot the serial log gets
   `qubix: Rust crypto up (ChaCha20-Poly1305)`.

`cryptotest` is a template for another cipher: it checks the self-test,
seal/open, and that a corrupted tag produces `-EBADMSG`.

## Replacing the cipher with another team's cipher

1. Keep `struct hix_crypto_req` and the operation numbers; if necessary, add
   new `op` values and fields and bump `HIX_CRYPTO_ABI`.
2. Implement the algorithm in `rust/hix-core/src/` (or connect a C module and
   keep a Rust wrapper). Preserve the `hix_rust_*` signatures or add new ones
   and update `qubix/crypto.c`.
3. Add a self-test with a published vector and a unit test.
4. Add vector generation to `scripts/crypto_check.py`; for a different format,
   you can generate a separate file and compare byte by byte.
5. Update `user/apps/cryptotest.c` for the new ABI and run all three
   verification levels.

If the algorithm already exists in Python, the fastest path is to obtain the
reference bytes first:

    from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
    ct = ChaCha20Poly1305(key).encrypt(nonce, plaintext, aad)
    ciphertext, tag = ct[:-16], ct[-16:]

The same bytes must appear from `cryptotest`/`crypto_check`; a mismatch is
printed as the first failing case.

## Limitations

- This is an educational/applied module, not an audited crypto library.
- No hardware acceleration, AES-NI, etc.; only portable integer code. The
  normal crypto path is SSE-free; SSE formatting code from the precompiled
  `core` panic path runs only on a fatal panic.
- No key management: the key comes from ring 3 on every call and is wiped
  afterwards. There is no persistent secret storage in the kernel.
- Python in ring 0 is not supported; a guest needs ring 3 (MicroPython or a
  separate interpreter).
- The module has not been tested on real hardware; QEMU and host tests are not
  a security audit.
