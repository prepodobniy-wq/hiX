; SPDX-License-Identifier: GPL-2.0-only
; probe3: a privileged instruction and a bad pointer; both must be refused.
%include "user/elfhdr.inc"

_start:
	; write() with a kernel pointer must fail with -EFAULT (-14)
	mov	eax, 4
	mov	ebx, 1
	mov	ecx, 0x00100000
	mov	edx, 16
	int	0x80
	cmp	eax, -14
	jne	.bad
	; a system call outside the ring 3 whitelist must fail with -ENOSYS
	mov	eax, 500
	int	0x80
	cmp	eax, -38
	jne	.bad
	mov	eax, 4
	mov	ebx, 1
	mov	ecx, ok
	mov	edx, oklen
	int	0x80
	cli				; privileged: #GP, the kernel kills us
.bad:
	mov	eax, 1
	mov	ebx, 1
	int	0x80

ok:	db	"probe3: bad pointer and bad syscall refused, now cli...", 10
oklen	equ $ - ok
filesize equ $ - $$
