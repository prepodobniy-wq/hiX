# How a release is prepared

An hix release is one tag `vX.Y.Z` and one set of files. The version is stored
only in `include/hix/version.h`; neither the README, nor the tests, nor the
Makefile contain a separate copy of the number.

## What `make release` does

`scripts/release.sh` intentionally does not touch git and does not publish
anything. It builds and verifies exactly what will go into the tag:

1. `make clean` — a clean build.
2. `make STRICT=1 RUST=1 all` — C/C++ with `-Werror`, the Rust crypto module
   is required, the ISO is required.
3. `make verify` — `check` (style + Multiboot), `test` (host tests),
   `rust-test`, `crypto-test`, `shots-check`.
4. `python3 scripts/smoke.py --require-rust` — boot in QEMU, work through
   COM1: `dmesg`, splash, `uname`, `crypto`, `sysctl hix.crypto`, `progs`,
   `kitinfo`, `lsbin`, `fstest`, `cryptotest`, `hixfetch`, the screensaver and
   `halt`.
5. Packaging.

The verification does not "trust" the previous build: `make verify` reruns the
host tests and compares frames, and the smoke test boots exactly
`build/hix.elf`.

## Result

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

`dist/SHA256SUMS` — checksums of the three published files (`hix.elf`,
`hix.iso`, `hix-1.0.0-src.tar.gz`) under the names they will have on the
release page: download them into one directory and check with
`sha256sum -c SHA256SUMS`. Inside `hix-1.0.0/SHA256SUMS` are the checksums of
the whole directory with documentation, logs and frames; check it from the
root of the unpacked package.

`BUILD-INFO` contains the version, date, git commit (for a checkout), compiler
versions and kernel size. Logs are attached so the verification is
reproducible.

## Before running

    make rust-sysroot        # if the i686 core is not available yet
    scripts/hixctl doctor    # gcc -m32, nasm, grub, qemu, rust
    make release

The script fails if `grub-mkrescue`, QEMU or i686 Rust is missing: a release
must not silently end up without an ISO or without the crypto module.
`DIST=/path` changes the output directory.

## Tag and publication

When `dist/` is verified, create the repository and tag (if they do not exist
yet):

    git init
    git add .
    git commit -m "hix 1.0.0"
    git tag -a v1.0.0 -m "hix 1.0.0"
    git remote add origin git@github.com:OWNER/hix.git
    git push -u origin main
    git push origin v1.0.0

Then publish the files:

    gh release create v1.0.0 \
        dist/hix-1.0.0/hix.elf \
        dist/hix-1.0.0/hix.iso \
        dist/hix-1.0.0-src.tar.gz \
        dist/SHA256SUMS \
        --title "hix 1.0.0" \
        --notes-file docs/releases/1.0.0.md

If GitHub CLI is not configured, attach the same four files manually on the
Releases page. `dist/hix-1.0.0/` (documentation and logs) can be attached in
full or left in CI artifacts — the ELF, ISO, sources and checksums are what
matter to the user.

## After publication

1. Download `hix.elf`, `hix.iso`, the sources and `SHA256SUMS` into a clean
   directory and verify: `sha256sum -c SHA256SUMS`.
2. Boot the ISO in QEMU: `qemu-system-i386 -m 128M -cdrom hix-1.0.0.iso`.
3. Check `hix.elf` with `-kernel` and `make smoke` from the source archive.
4. Make sure `BUILD-INFO` and `uname -a` report the same version.

## What the script does not do

- it does not create or move tags;
- it does not call `gh`;
- it does not publish to GitHub Packages;
- it does not build binaries for other platforms (hix is i386 only).

This is intentional: building artifacts and publishing are separate decisions.
