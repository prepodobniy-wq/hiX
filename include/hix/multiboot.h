/* SPDX-License-Identifier: GPL-2.0-only */
/* Multiboot 0.6.96 structures (what GRUB hands us in %ebx). */
#ifndef _HIX_MULTIBOOT_H
#define _HIX_MULTIBOOT_H

#include <hix/types.h>
#include <hix/compiler.h>

#define MULTIBOOT_BOOTLOADER_MAGIC	0x2BADB002

#define MB_INFO_MEMORY		BIT(0)
#define MB_INFO_BOOTDEV		BIT(1)
#define MB_INFO_CMDLINE		BIT(2)
#define MB_INFO_MODS		BIT(3)
#define MB_INFO_MMAP		BIT(6)

#define MB_MMAP_AVAILABLE	1

struct multiboot_info {
	u32 flags;
	u32 mem_lower;		/* KiB below 1 MiB */
	u32 mem_upper;		/* KiB above 1 MiB */
	u32 boot_device;
	u32 cmdline;
	u32 mods_count;
	u32 mods_addr;
	u32 syms[4];
	u32 mmap_length;
	u32 mmap_addr;
} __packed;

/* @size does not include itself, entries are walked with size + 4 */
struct multiboot_mmap_entry {
	u32 size;
	u32 base_low;
	u32 base_high;
	u32 length_low;
	u32 length_high;
	u32 type;
} __packed;

struct multiboot_module {
	u32 mod_start;
	u32 mod_end;
	u32 string;
	u32 reserved;
} __packed;

#endif /* _HIX_MULTIBOOT_H */
