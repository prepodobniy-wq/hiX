// SPDX-License-Identifier: GPL-2.0-only
/* panic(): the last words of the kernel. */
#include <stdarg.h>
#include <hix/kernel.h>
#include <hix/version.h>
#include <asm/trap.h>
#include <asm/irqflags.h>
#include <aether/task.h>
#include <steel/steel.h>

void printk_bust_locks(void);

static int panicking;

static void panic_begin(const char *msg)
{
	struct thread *t;

	local_irq_disable();
	if (panicking)			/* panic inside panic: give up quietly */
		arch_halt();
	panicking = 1;
	printk_bust_locks();
	steel_con_set_suspended(false);	/* the report must reach the screen */

	pr_emerg("\n");
	pr_emerg("Kernel panic - not syncing: %s\n", msg);
	t = thread_current();
	pr_emerg("%s %s, thread: %s (tid %d)\n", HIX_NAME, HIX_RELEASE,
		 t ? t->name : "none", t ? t->tid : -1);
}

static void panic_end(void)
{
	pr_emerg("---[ end Kernel panic - not syncing ]---\n");
	arch_halt();
}

void panic(const char *fmt, ...)
{
	char msg[160];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	panic_begin(msg);
	{
		unsigned long ebp;

		__asm__ volatile("mov %%ebp, %0" : "=r"(ebp));
		dump_stack_trace(ebp);
	}
	panic_end();
	for (;;)
		;
}

void panic_regs(const struct pt_regs *regs, const char *fmt, ...)
{
	char msg[160];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	panic_begin(msg);
	dump_regs(regs);
	dump_stack_trace(regs->ebp);
	panic_end();
	for (;;)
		;
}

/* entered on the dedicated double fault stack, see arch/x86/gdt.c */
void double_fault_report(const struct pt_regs *regs)
{
	local_irq_disable();
	panic_begin("double fault");
	pr_emerg("A fault happened while handling a fault. The usual cause is\n"
		 "a kernel stack overflow (the guard page was hit): ESP=%08x\n",
		 regs->esp_dummy);
	dump_regs(regs);
	dump_stack_trace(regs->ebp);
	panic_end();
	for (;;)
		;
}
