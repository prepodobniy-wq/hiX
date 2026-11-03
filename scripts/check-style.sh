#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# A tiny stand-in for scripts/checkpatch.pl: enforces the parts of the Linux
# coding style that are mechanical. Exit status 1 when something is wrong.
#   - indentation is tabs (no line starts with spaces)
#   - no trailing whitespace
#   - lines are at most 100 columns (checkpatch limit; 80 preferred)
#   - every source file starts with an SPDX-License-Identifier line

cd "$(dirname "$0")/.." || exit 1
files=$(find arch aether qubix steel apps drivers kernel lib include tests \
	-name '*.c' -o -name '*.h' -o -name '*.asm' | grep -v "/build/\|gfx_data.c" | sort)
out=$(mktemp)
trap 'rm -f "$out"' EXIT

for f in $files; do
	grep -nE '^ +[^ *]' "$f" | grep -v '^[0-9]*: *\*' | head -3 |
	while IFS= read -r l; do echo "style: $f:$l  (indent with tabs)"; done
	grep -nE '[[:space:]]+$' "$f" | head -3 |
	while IFS= read -r l; do echo "style: $f:$l  (trailing whitespace)"; done
	expand -t8 "$f" | awk -v f="$f" 'length($0) > 100 \
		{ printf "style: %s:%d: %d columns\n", f, NR, length($0) }' | head -3
	head -3 "$f" | grep -q 'SPDX-License-Identifier' ||
		echo "style: $f: missing SPDX line"
done > "$out"

cat "$out"
n=$(wc -l < "$out")
if [ "$n" -gt 0 ]; then
	echo "style check: $n problem(s)"
	exit 1
fi
echo "style check OK ($(echo "$files" | wc -l) files)"
