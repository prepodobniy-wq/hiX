// SPDX-License-Identifier: GPL-2.0-only
/*
 * Trap dispatch: CPU exceptions, hardware interrupts and system calls all
 * arrive here from entry.asm through do_trap().
 */
#define pr_fmt(fmt) "traps: " fmt

#include <hix/kernel.h>
#include <asm/trap.h>
#include <asm/irqflags.h>
#include <aether/sched.h>
#include <aether/vm.h>

/* entry.asm builds this layout: 16 (segments) + 32 (pusha) + 8 */
_Static_assert(offsetof(struct pt_regs, eip) == 56,
	       "struct pt_regs does not match arch/x86/entry.asm");
_Static_assert(offsetof(struct pt_regs, vector) == 48,
	       "struct pt_regs does not match arch/x86/entry.asm");

extern void (*const trap_stub_table[48])(void);
extern void trap_stub_128(void);

volatile int need_resched;

static const char * const exception_names[32] = {
	"divide error",
	"debug",
	"non-maskable interrupt",
	"breakpoint",
	"overflow",
	"bound range exceeded",
	"invalid opcode",
	"device not available",
	"double fault",
	"coprocessor segment overrun",
	"invalid TSS",
	"segment not present",
	"stack-segment fault",
	"general protection fault",
	"page fault",
	"reserved",
	"x87 floating-point error",
	"alignment check",
	"machine check",
	"SIMD floating-point error",
	"virtualization error",
	"control protection error",
	"reserved", "reserved", "reserved", "reserved", "reserved", "reserved",
	"hypervisor injection error",
	"VMM communication error",
	"security exception",
	"reserved",
};

struct irq_action {
	irq_handler_t handler;
	void *dev;
	const char *name;
	unsigned long count;
};

static struct irq_action irq_actions[NR_IRQS];
static unsigned long spurious_irqs;

static void (*syscall_handler)(struct pt_regs *regs);

int request_irq(int irq, irq_handler_t handler, const char *name, void *dev)
{
	unsigned long flags;

	if (irq < 0 || irq >= NR_IRQS || irq == 2 || !handler)
		return -EINVAL;

	local_irq_save(flags);
	if (irq_actions[irq].handler) {
		local_irq_restore(flags);
		return -EBUSY;
	}
	irq_actions[irq].handler = handler;
	irq_actions[irq].dev = dev;
	irq_actions[irq].name = name;
	irq_actions[irq].count = 0;
	pic_unmask(irq);
	local_irq_restore(flags);
	return 0;
}

void free_irq(int irq)
{
	unsigned long flags;

	if (irq < 0 || irq >= NR_IRQS)
		return;
	local_irq_save(flags);
	pic_mask(irq);
	irq_actions[irq].handler = NULL;
	local_irq_restore(flags);
}

void enable_irq(int irq)
{
	pic_unmask(irq);
}

void disable_irq(int irq)
{
	pic_mask(irq);
}

void set_syscall_handler(void (*handler)(struct pt_regs *regs))
{
	syscall_handler = handler;
}

const char *irq_name(int irq, unsigned long *count)
{
	if (irq < 0 || irq >= NR_IRQS || !irq_actions[irq].handler)
		return NULL;
	if (count)
		*count = irq_actions[irq].count;
	return irq_actions[irq].name;
}

static void do_irq(struct pt_regs *regs)
{
	int irq = (int)regs->vector - IRQ0_VECTOR;
	struct irq_action *act = &irq_actions[irq];

	if (pic_irq_is_spurious(irq)) {
		spurious_irqs++;
		return;
	}

	/*
	 * Acknowledge first: interrupts stay off until iret, so the line
	 * cannot re-enter, and a reschedule below must not leave the PIC
	 * waiting for an EOI that would only come when we are resumed.
	 */
	pic_eoi(irq);

	if (act->handler) {
		act->count++;
		act->handler(irq, act->dev);
	} else {
		pr_warn("unhandled IRQ %d\n", irq);
		pic_mask(irq);
	}

	if (need_resched)
		schedule();
}

static void do_exception(struct pt_regs *regs)
{
	unsigned int vec = regs->vector;

	if (vec == X86_TRAP_PF) {
		unsigned long addr = read_cr2();
		const char *why = vm_fault_reason(addr);

		panic_regs(regs, "page fault at %08x (%s%s%s)%s%s",
			   (u32)addr,
			   (regs->error_code & 1) ? "protection violation"
						  : "page not present",
			   (regs->error_code & 2) ? ", write" : ", read",
			   (regs->error_code & 4) ? ", user" : ", kernel",
			   why ? ": " : "", why ? why : "");
	}

	panic_regs(regs, "unhandled exception %u: %s (error code %08x)",
		   vec, vec < 32 ? exception_names[vec] : "?",
		   regs->error_code);
}

/* called from trap_common in entry.asm with interrupts disabled */
void do_trap(struct pt_regs *regs)
{
	if (regs->vector == SYSCALL_VECTOR) {
		if (!syscall_handler)
			panic_regs(regs, "system call before qubix is up");
		local_irq_enable();
		syscall_handler(regs);
		local_irq_disable();
		if (need_resched)
			schedule();
	} else if (regs->vector >= IRQ0_VECTOR &&
		   regs->vector < IRQ0_VECTOR + NR_IRQS) {
		do_irq(regs);
	} else {
		do_exception(regs);
	}
}

void arch_init_traps(void)
{
	int i;

	for (i = 0; i < 48; i++)
		idt_set_gate(i, trap_stub_table[i], 0x8E);
	idt_set_gate(SYSCALL_VECTOR, trap_stub_128, 0xEE);
	idt_set_task_gate(8, gdt_df_selector());
}
