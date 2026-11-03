// SPDX-License-Identifier: GPL-2.0-only
/* Interrupt Descriptor Table. */

#include <hix/kernel.h>
#include <asm/trap.h>

struct idt_entry {
	u16 offset_low;
	u16 selector;
	u8  zero;
	u8  type_attr;
	u16 offset_high;
} __packed;

struct desc_ptr {
	u16 limit;
	u32 base;
} __packed;

#define IDT_ENTRIES	256

static struct idt_entry idt[IDT_ENTRIES];
static struct desc_ptr idtr;

extern void idt_load(const struct desc_ptr *idtr);

/*
 * @type_attr: 0x8E interrupt gate (IF cleared on entry), DPL 0
 *             0xEE interrupt gate, DPL 3 (system call)
 *             0x85 task gate (double fault; @handler is then the TSS selector)
 */
void idt_set_gate(int vector, void (*handler)(void), u8 type_attr)
{
	u32 addr = (u32)(uintptr_t)handler;

	idt[vector].offset_low = addr & 0xFFFF;
	idt[vector].selector = KERNEL_CS;
	idt[vector].zero = 0;
	idt[vector].type_attr = type_attr;
	idt[vector].offset_high = (addr >> 16) & 0xFFFF;
}

/* task gates ignore the offset and carry the TSS selector instead */
void idt_set_task_gate(int vector, u16 tss_selector)
{
	idt[vector].offset_low = 0;
	idt[vector].selector = tss_selector;
	idt[vector].zero = 0;
	idt[vector].type_attr = 0x85;
	idt[vector].offset_high = 0;
}

void idt_init(void)
{
	idtr.limit = sizeof(idt) - 1;
	idtr.base = (u32)(uintptr_t)idt;
	idt_load(&idtr);
}
