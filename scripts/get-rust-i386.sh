#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Fetch the i386 Rust core/std development files into .rust-sysroot.
#
# This is for Debian/Ubuntu hosts where rustc is installed as a package but
# `rustup` is not. It downloads libstd-rust-dev:i386 and extracts only the
# rustlib directory; nothing is installed system-wide and no root is needed.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
target=.rust-sysroot
pkg="libstd-rust-dev:i386"

command -v apt-get >/dev/null 2>&1 || {
	echo "get-rust-i386: apt-get not found; use rustup target add i686-unknown-linux-gnu" >&2
	exit 1
}
command -v dpkg-deb >/dev/null 2>&1 || {
	echo "get-rust-i386: dpkg-deb not found" >&2
	exit 1
}
if ! apt-cache show "$pkg" >/dev/null 2>&1; then
	echo "get-rust-i386: $pkg not found in apt" >&2
	echo "  enable the i386 architecture first:" >&2
	echo "    sudo dpkg --add-architecture i386 && sudo apt-get update" >&2
	exit 1
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

echo "get-rust-i386: downloading $pkg"
(cd "$tmp" && apt-get download "$pkg" >/dev/null)

deb=$(echo "$tmp"/*.deb)
dpkg-deb -x "$deb" "$tmp/root"

mkdir -p "$root/$target/lib/rustlib"
rm -rf "$root/$target/lib/rustlib/i686-unknown-linux-gnu"
cp -a "$tmp/root/usr/lib/rustlib/i686-unknown-linux-gnu" \
	"$root/$target/lib/rustlib/"

echo "get-rust-i386: ready: $root/$target"
echo "get-rust-i386: now run 'make rust' or just 'make'"
