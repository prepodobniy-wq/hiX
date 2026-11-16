#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Build rust/hix-core into a static library for the i386 kernel.
set -eu

. "$(dirname -- "$0")/rust-common.sh"

out=${1:-"$HIX_ROOT/build/rust/libhix_rust.a"}

if ! hix_rust_available; then
	echo "rust: no i686 core library found" >&2
	echo "      Debian/Ubuntu: scripts/get-rust-i386.sh" >&2
	echo "      rustup:        rustup target add i686-unknown-linux-gnu" >&2
	exit 1
fi

mkdir -p "$(dirname -- "$out")"

set -- --edition 2021 \
	--crate-name hix_rust \
	--crate-type staticlib \
	--target "$HIX_RUST_TARGET" \
	--cfg 'feature="kernel"' \
	-C opt-level=2 \
	-C panic=abort \
	-C relocation-model=static \
	-C target-feature=-sse,-sse2,-fxsr \
	-C debuginfo=2

if [ -n "$HIX_RUST_SYSROOT" ]; then
	set -- "$@" --sysroot "$HIX_RUST_SYSROOT"
fi

rustc "$@" -o "$out" "$HIX_ROOT/rust/hix-core/src/lib.rs"
