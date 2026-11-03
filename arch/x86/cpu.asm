; SPDX-License-Identifier: GPL-2.0-only
;
; arch/x86/cpu.asm - descriptor table loading

bits 32

KERNEL_CS	equ 0x08
KERNEL_DS	equ 0x10

section .text
global gdt_flush
global idt_load
global tss_load

; void gdt_flush(const struct desc_ptr *gdtr)
gdt_flush:
	mov	eax, [esp + 4]
	lgdt	[eax]
	mov	ax, KERNEL_DS
	mov	ds, ax
	mov	es, ax
	mov	fs, ax
	mov	gs, ax
	mov	ss, ax
	jmp	KERNEL_CS:.reload	; far jump reloads %cs
.reload:
	ret

; void idt_load(const struct desc_ptr *idtr)
idt_load:
	mov	eax, [esp + 4]
	lidt	[eax]
	ret

; void tss_load(u16 selector)
tss_load:
	mov	ax, [esp + 4]
	ltr	ax
	ret
