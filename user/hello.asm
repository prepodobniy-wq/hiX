; SPDX-License-Identifier: GPL-2.0-only
; hello3: first program that runs in ring 3.
%include "user/elfhdr.inc"

_start:
	mov	eax, 20			; getpid
	int	0x80
	mov	esi, eax
	mov	eax, 4			; write(1, msg, len)
	mov	ebx, 1
	mov	ecx, msg
	mov	edx, msglen
	int	0x80
	mov	eax, 1			; exit(0)
	xor	ebx, ebx
	int	0x80

msg:	db	"hello from ring 3: this text came through int 0x80", 10
msglen	equ $ - msg
filesize equ $ - $$
