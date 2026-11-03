/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

#define X86_EFLAGS_IF	0x00000200UL

#ifdef HIX_HOST_TEST

static inline unsigned long arch_local_save_flags(void) { return 0; }
static inline void arch_local_irq_restore(unsigned long f) { (void)f; }
static inline void local_irq_enable(void) { }
static inline void local_irq_disable(void) { }
static inline void cpu_relax(void) { }

#else

static inline unsigned long arch_local_save_flags(void)
{
	unsigned long flags;

	__asm__ volatile("pushf; pop %0" : "=r"(flags) : : "memory");
	return flags;
}

static inline void local_irq_disable(void)
{
	__asm__ volatile("cli" : : : "memory");
}

static inline void local_irq_enable(void)
{
	__asm__ volatile("sti" : : : "memory");
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	if (flags & X86_EFLAGS_IF)
		local_irq_enable();
	else
		local_irq_disable();
}

static inline void cpu_relax(void)
{
	__asm__ volatile("pause" : : : "memory");
}

#endif /* HIX_HOST_TEST */

#define local_save_flags(flags)	((flags) = arch_local_save_flags())
#define local_irq_save(flags)			\
	do {					\
		local_save_flags(flags);	\
		local_irq_disable();		\
	} while (0)
#define local_irq_restore(flags)	arch_local_irq_restore(flags)

static inline int irqs_disabled(void)
{
	return !(arch_local_save_flags() & X86_EFLAGS_IF);
}

#endif /* _ASM_IRQFLAGS_H */
