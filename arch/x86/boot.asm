; SPDX-License-Identifier: GPL-2.0-only
;
; arch/x86/boot.asm - hix entry point (Multiboot 0.6.96, i386)
;
; GRUB loads us at 1 MiB in 32-bit protected mode with paging off, interrupts
; off, %eax = multiboot magic and %ebx = physical address of the info block.

bits 32

MB_MAGIC	equ 0x1BADB002
MB_FLAGS	equ 0x00000003		; bit 0: align modules, bit 1: memory info
MB_CHECKSUM	equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 4
	dd MB_MAGIC
	dd MB_FLAGS
	dd MB_CHECKSUM

; The boot stack later becomes the stack of the idle thread. It is page
; aligned and preceded by a guard page that vm_init() unmaps, so that an
; overflow faults instead of silently eating .bss.
section .bss
align 4096
global boot_stack_guard
global boot_stack_bottom
global boot_stack_top
boot_stack_guard:
	resb 4096
boot_stack_bottom:
	resb 16384
boot_stack_top:

section .text
global _start
extern hix_start

_start:
	cli
	cld
	mov	esp, boot_stack_top
	xor	ebp, ebp		; end of the frame-pointer chain
	push	ebx			; arg 2: struct multiboot_info *
	push	eax			; arg 1: multiboot magic
	call	hix_start		; never returns
.hang:
	cli
	hlt
	jmp	.hang
