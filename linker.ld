/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * hix linker script: a single image at 1 MiB (Multiboot).
 *
 * The section boundaries are page aligned on purpose: aether/vm.c makes
 * everything up to __ro_end read-only (text + rodata) with 4 KiB pages.
 */
ENTRY(_start)

SECTIONS
{
	. = 1M;
	__kernel_start = .;

	.text : ALIGN(4K)
	{
		*(.multiboot)		/* must be within the first 8 KiB */
		*(.text .text.*)
	}

	.rodata : ALIGN(4K)
	{
		*(.rodata .rodata.*)

		/* the driver registry, see include/hix/driver.h */
		. = ALIGN(4);
		__drivers_start = .;
		KEEP(*(.drivers))
		__drivers_end = .;
	}
	. = ALIGN(4K);
	__ro_end = .;

	.data : ALIGN(4K)
	{
		*(.data .data.*)

		/* constructors of global C++ objects, see lib/cxxrt.c */
		. = ALIGN(4);
		__init_array_start = .;
		KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) .init_array))
		__init_array_end = .;
	}

	.bss : ALIGN(4K)
	{
		__bss_start = .;
		*(COMMON)
		*(.bss .bss.*)
		__bss_end = .;
	}

	. = ALIGN(4K);
	__kernel_end = .;

	/DISCARD/ :
	{
		*(.comment)
		*(.note .note.*)
		*(.eh_frame .eh_frame.*)
	}
}
