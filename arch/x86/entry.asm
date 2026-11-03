; SPDX-License-Identifier: GPL-2.0-only
;
; arch/x86/entry.asm - interrupt, exception and system call entry stubs
;
; Every vector gets a tiny stub that normalises the stack (adds a fake error
; code when the CPU does not push one, then pushes the vector number) and
; jumps to trap_common, which builds a struct pt_regs and calls do_trap().
; The layout must match struct pt_regs in include/asm/trap.h.

bits 32

KERNEL_DS	equ 0x10

%macro TRAP_NOERR 1
trap_stub_%1:
	push	dword 0			; fake error code
	push	dword %1		; vector
	jmp	trap_common
%endmacro

%macro TRAP_ERR 1
trap_stub_%1:
	push	dword %1		; vector (CPU already pushed the error code)
	jmp	trap_common
%endmacro

section .text
extern do_trap

; exceptions 0-31 and IRQs 0-15 (vectors 32-47)
%assign i 0
%rep 48
%if (i == 8) || (i >= 10 && i <= 14) || (i == 17) || (i == 21) || (i == 29) || (i == 30)
	TRAP_ERR %[i]
%else
	TRAP_NOERR %[i]
%endif
%assign i i+1
%endrep

; system call gate, vector 0x80
global trap_stub_128
TRAP_NOERR 128

trap_common:
	pusha
	push	ds
	push	es
	push	fs
	push	gs
	mov	ax, KERNEL_DS
	mov	ds, ax
	mov	es, ax
	mov	fs, ax
	mov	gs, ax
	cld
	push	esp			; struct pt_regs *
	call	do_trap
	add	esp, 4
	pop	gs
	pop	fs
	pop	es
	pop	ds
	popa
	add	esp, 8			; vector + error code
	iret

section .rodata
global trap_stub_table
trap_stub_table:
%assign i 0
%rep 48
	dd	trap_stub_ %+ i
%assign i i+1
%endrep
