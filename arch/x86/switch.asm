; SPDX-License-Identifier: GPL-2.0-only
;
; arch/x86/switch.asm - kernel thread context switch
;
; Only the callee-saved registers (cdecl) travel with a thread; everything
; else is either dead across the call or sits in the interrupt frame.

bits 32

section .text
extern thread_finish
global ctx_switch
global thread_trampoline

; void ctx_switch(unsigned long *old_sp, unsigned long new_sp);
; Must be called with interrupts disabled.
ctx_switch:
	mov	eax, [esp + 4]		; where to save our stack pointer
	mov	edx, [esp + 8]		; stack pointer of the thread to run
	push	ebp
	push	ebx
	push	esi
	push	edi
	mov	[eax], esp
	mov	esp, edx
	pop	edi
	pop	esi
	pop	ebx
	pop	ebp
	ret

; A new thread's first "return" from ctx_switch lands here, with
;   %ebx = entry function, %esi = its argument (see thread_prepare_stack()).
; Interrupts are still off because schedule() runs with them off.
thread_trampoline:
%ifndef HOSTTEST
	sti
%endif
	push	esi
	call	ebx
	add	esp, 4
	call	thread_finish		; entry function returned: exit the thread
.hang:
	hlt
	jmp	.hang
