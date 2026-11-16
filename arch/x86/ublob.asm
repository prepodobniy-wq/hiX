; SPDX-License-Identifier: GPL-2.0-only
;
; arch/x86/ublob.asm - the ring 3 demo programs, linked into the kernel image
; (assembled from user/*.asm into build/user/*.elf by the Makefile)

bits 32
section .rodata

global user_hello_start, user_hello_end
user_hello_start:
	incbin	"hello.elf"
user_hello_end:

global user_fault_start, user_fault_end
user_fault_start:
	incbin	"fault.elf"
user_fault_end:

global user_initfs_start, user_initfs_end
user_initfs_start:
	incbin	"initfs.bin"
user_initfs_end:

global user_probe_start, user_probe_end
user_probe_start:
	incbin	"probe.elf"
user_probe_end:
