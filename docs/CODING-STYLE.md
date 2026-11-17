# Coding style

The code follows the Linux kernel coding style (`Documentation/process/coding-style.rst`):

- indentation with tabs (width 8), K&R braces, function definitions start on a new line;
- `snake_case` names, no typedefs for structs, `/* */` comments (`//` is allowed in the header of a `.c` file);
- 80 columns preferred, 100 hard limit (as in checkpatch);
- the first line of a file is `SPDX-License-Identifier: GPL-2.0-only`
  (replace it with a single `sed` command if you need a different license);
- errors use `-E*` (Linux numbering), `list_head`, `container_of`, `printk` with `KERN_*` levels;
- `scripts/check-style.sh` checks the mechanical rules; `.clang-format` and `.editorconfig` are at the root.

The Linux interfaces themselves (syscalls, ABI) are not copied: hix has its own —
positive numbers for processes, negative ones for aether ports (see ARCHITECTURE.md).
