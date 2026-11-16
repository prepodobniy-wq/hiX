#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Build a release the way the tag should be built:
#
#   make clean
#   make STRICT=1 RUST=1 all       strict C, Rust crypto required, ISO
#   make verify                    host tests, Rust, crypto, style, frames
#   scripts/smoke.py --require-rust   boot in QEMU and drive the shell
#   dist/hix-<version>/            ELF, ISO, docs, logs, BUILD-INFO
#   dist/hix-<version>-src.tar.gz  source archive
#   dist/SHA256SUMS                checksums for both
#
# Nothing here touches git: tagging and uploading are separate, deliberate
# steps (see docs/RELEASE.md). DIST=/some/dir moves the output elsewhere.
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

say() { printf 'release: %s\n' "$*"; }
die() { printf 'release: %s\n' "$*" >&2; exit 1; }

VERSION=$(make --no-print-directory version)
[ -n "$VERSION" ] || die "cannot read HIX_RELEASE from include/hix/version.h"

DIST=${DIST:-$ROOT/dist}
OUT=$DIST/hix-$VERSION
SRC=$DIST/hix-$VERSION-src.tar.gz

for tool in make gcc g++ ld nasm python3 qemu-system-i386 cargo rustc; do
	command -v "$tool" >/dev/null 2>&1 ||
		die "$tool not found; see docs/INSTALL.md"
done
if ! command -v grub-mkrescue >/dev/null 2>&1 &&
   ! command -v grub2-mkrescue >/dev/null 2>&1; then
	die "grub-mkrescue not found; the ISO is part of the release"
fi
scripts/rust-available.sh ||
	die "no i686 Rust core; run 'make rust-sysroot' first"

rm -rf "$OUT" "$SRC"
mkdir -p "$OUT"

say "strict Rust build ($VERSION)"
make clean
if ! make STRICT=1 RUST=1 all 2>&1 | tee "$OUT/build.log"; then
	die "build failed; see $OUT/build.log"
fi
[ -s build/hix.elf ] || die "build/hix.elf is missing"
[ -s build/hix.iso ] || die "build/hix.iso is missing"

say "host verification"
if ! make verify 2>&1 | tee "$OUT/verify.log"; then
	die "verification failed; see $OUT/verify.log"
fi

say "boot smoke test (QEMU, COM1)"
if ! python3 scripts/smoke.py --require-rust --logdir "$OUT/smoke" \
	2>&1 | tee "$OUT/smoke.log"; then
	die "smoke test failed; see $OUT/smoke.log"
fi

say "packaging"
cp build/hix.elf build/hix.iso "$OUT/"
cp README.md LICENSE "$OUT/"
cp -R docs "$OUT/docs"
mkdir -p "$OUT/config"
cp config/hix.conf "$OUT/config/hix.conf"
cp iso/boot/grub/grub.cfg "$OUT/grub.cfg"

{
	printf 'hix %s\n' "$VERSION"
	printf 'built: %s\n' "$(date -u '+%Y-%m-%d %H:%M:%S UTC')"
	if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
		printf 'git: %s\n' "$(git rev-parse --short HEAD)"
	else
		printf 'git: not a git checkout\n'
	fi
	printf 'host: %s\n' "$(uname -srm)"
	printf 'cc: %s\n' "$(cc --version | head -n 1)"
	printf 'rustc: %s\n' "$(rustc --version)"
	printf 'cargo: %s\n' "$(cargo --version)"
	printf 'qemu: %s\n' "$(qemu-system-i386 --version | head -n 1)"
	printf 'rust crypto: built in\n'
	printf 'smoke: passed (scripts/smoke.py --require-rust)\n'
	printf 'kernel size:\n'
	size build/hix.elf 2>/dev/null || true
} > "$OUT/BUILD-INFO"

say "source archive"
tar --format=gnu -czf "$SRC" \
	--exclude='./build' --exclude='./dist' --exclude='./.git' \
	--exclude='./.rust-sysroot' --exclude='./tests/build' \
	--exclude='*/target' --exclude='*/__pycache__' \
	--transform "s,^\./,hix-$VERSION/," .
[ -s "$SRC" ] || die "source archive is empty"

say "checksums"
# The public SHA256SUMS uses the flat names from the release page, so a user
# who downloads hix.elf, hix.iso, the source archive and this file can run
# `sha256sum -c SHA256SUMS` in one directory.
(
	cd "$OUT" && sha256sum hix.elf hix.iso
	cd "$DIST" && sha256sum "$(basename "$SRC")"
) > "$DIST/SHA256SUMS"
# The package carries its own sums for the full directory (docs, logs, shots).
(
	cd "$OUT"
	find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum
) > "$OUT/SHA256SUMS"

say "done: $OUT"
say "      $SRC"
say "      $DIST/SHA256SUMS"
say "next: docs/RELEASE.md (tag v$VERSION, upload the files above)"
