; SPDX-License-Identifier: GPL-2.0-only
; One-segment ELF32 header for tiny hand written ring 3 programs.
; The whole file is loaded at USER_BASE (read, write, execute).
bits 32
org 0x40000000

ehdr:
	db	0x7f, 'E', 'L', 'F', 1, 1, 1, 0
	times 8 db 0
	dw	2			; ET_EXEC
	dw	3			; EM_386
	dd	1
	dd	_start
	dd	phdr - $$
	dd	0
	dd	0
	dw	phdr - ehdr
	dw	32
	dw	1
	dw	0, 0, 0
phdr:
	dd	1			; PT_LOAD
	dd	0
	dd	$$
	dd	$$
	dd	filesize
	dd	filesize
	dd	7			; R W X
	dd	0x1000
