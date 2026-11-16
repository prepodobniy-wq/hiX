; SPDX-License-Identifier: GPL-2.0-only
; fault3: touches kernel memory; the kernel must kill it and keep running.
%include "user/elfhdr.inc"

_start:
	mov	eax, 4			; say what we are about to do
	mov	ebx, 1
	mov	ecx, msg
	mov	edx, msglen
	int	0x80
	mov	eax, [0x00100000]	; kernel text: supervisor only
	mov	eax, 1
	xor	ebx, ebx
	int	0x80

msg:	db	"fault3: reading kernel memory from ring 3...", 10
msglen	equ $ - msg
filesize equ $ - $$
