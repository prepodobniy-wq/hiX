/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_TRAP_H
#define _ASM_TRAP_H

#include <hix/types.h>

/* GDT selectors, see arch/x86/gdt.c */
#define KERNEL_CS	0x08
#define KERNEL_DS	0x10
#define USER_CS		0x1B
#define USER_DS		0x23

#define X86_TRAP_PF		14
#define IRQ0_VECTOR		32
#define NR_IRQS			16
#define SYSCALL_VECTOR		0x80

/*
 * Register frame built by arch/x86/entry.asm on every interrupt, exception
 * and system call. The order is the order of the pushes, bottom to top.
 */
struct pt_regs {
	u32 gs, fs, es, ds;
	u32 edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;	/* pusha */
	u32 vector, error_code;
	u32 eip, cs, eflags;
	/* only present when the CPU switched privilege level: */
	u32 user_esp, user_ss;
};

/* arch/x86/traps.c */
typedef void (*irq_handler_t)(int irq, void *dev);

int request_irq(int irq, irq_handler_t handler, const char *name, void *dev);
void free_irq(int irq);
void enable_irq(int irq);
void disable_irq(int irq);
const char *irq_name(int irq, unsigned long *count);	/* NULL: unused */

/* qubix installs its system call dispatcher here */
void set_syscall_handler(void (*handler)(struct pt_regs *regs));

/* the scheduler asks for a reschedule from interrupt context */
extern volatile int need_resched;

void arch_init_traps(void);

/* arch/x86/idt.c */
void idt_set_gate(int vector, void (*handler)(void), u8 type_attr);
void idt_set_task_gate(int vector, u16 tss_selector);
void idt_init(void);

/* arch/x86/gdt.c */
void gdt_init(void);
void gdt_set_df_cr3(unsigned long cr3);
void gdt_set_kernel_stack(unsigned long esp0);
u16 gdt_df_selector(void);

/* kernel/panic.c: report from the double fault task, never returns */
void double_fault_report(const struct pt_regs *regs) __attribute__((noreturn));

/* arch/x86/pic.c */
void pic_init(void);
void pic_mask(int irq);
void pic_unmask(int irq);
void pic_eoi(int irq);
bool pic_irq_is_spurious(int irq);

/* arch/x86/cpu.c */
unsigned long read_cr0(void);
unsigned long read_cr2(void);
unsigned long read_cr3(void);
unsigned long read_cr4(void);
void write_cr0(unsigned long v);
void write_cr3(unsigned long v);
void write_cr4(unsigned long v);
void invlpg(unsigned long addr);
void dump_regs(const struct pt_regs *regs);
void dump_stack_trace(unsigned long ebp);
void arch_reboot(void) __attribute__((noreturn));
void arch_poweroff(void);
void arch_halt(void) __attribute__((noreturn));

#endif /* _ASM_TRAP_H */
