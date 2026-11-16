#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Shared by scripts/rust-available.sh and scripts/rust-build.sh.
#
# hix uses the stable Rust i686 Linux target as a no_std target. The kernel
# never calls Linux, but the precompiled core for this target is what lets a
# normal Debian/Ubuntu rustc build the module without nightly or build-std.
# Set RUST_SYSROOT to use a local sysroot (scripts/get-rust-i386.sh).

HIX_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
HIX_RUST_TARGET=i686-unknown-linux-gnu
HIX_RUST_SYSROOT=${RUST_SYSROOT:-}

if [ -z "$HIX_RUST_SYSROOT" ] &&
   [ -d "$HIX_ROOT/.rust-sysroot/lib/rustlib/$HIX_RUST_TARGET" ]; then
	HIX_RUST_SYSROOT="$HIX_ROOT/.rust-sysroot"
fi

hix_rust_available()
{
	command -v rustc >/dev/null 2>&1 || return 1
	if [ -n "$HIX_RUST_SYSROOT" ]; then
		libdir="$HIX_RUST_SYSROOT/lib/rustlib/$HIX_RUST_TARGET/lib"
	else
		libdir=$(rustc --print target-libdir --target "$HIX_RUST_TARGET" 2>/dev/null) || return 1
	fi
	ls "$libdir"/libcore-*.rlib >/dev/null 2>&1
}
