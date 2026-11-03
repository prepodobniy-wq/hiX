/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hix/io.h - port I/O, interrupt flag and control register helpers (i386)
 */
#ifndef _HIX_IO_H
#define _HIX_IO_H

#include <hix/types.h>

static inline void outb(u16 port, u8 val)
{
	__asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline u8 inb(u16 port)
{
	u8 val;

	__asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}

static inline void outw(u16 port, u16 val)
{
	__asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline u16 inw(u16 port)
{
	u16 val;

	__asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}

static inline void io_wait(void)
{
	outb(0x80, 0);
}

static inline void cpu_relax(void)
{
	__asm__ volatile ("pause" : : : "memory");
}

static inline void irq_enable(void)
{
	__asm__ volatile ("sti" : : : "memory");
}

static inline void irq_disable(void)
{
	__asm__ volatile ("cli" : : : "memory");
}

static inline void cpu_halt(void)
{
	__asm__ volatile ("hlt" : : : "memory");
}

/* Save EFLAGS and disable interrupts; pair with irq_restore(). */
static inline u32 irq_save(void)
{
	u32 flags;

	__asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
	return flags;
}

static inline void irq_restore(u32 flags)
{
	__asm__ volatile ("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}

static inline u32 read_cr0(void)
{
	u32 v;

	__asm__ volatile ("movl %%cr0, %0" : "=r"(v));
	return v;
}

static inline void write_cr0(u32 v)
{
	__asm__ volatile ("movl %0, %%cr0" : : "r"(v) : "memory");
}

static inline u32 read_cr2(void)
{
	u32 v;

	__asm__ volatile ("movl %%cr2, %0" : "=r"(v));
	return v;
}

static inline u32 read_cr3(void)
{
	u32 v;

	__asm__ volatile ("movl %%cr3, %0" : "=r"(v));
	return v;
}

static inline void write_cr3(u32 v)
{
	__asm__ volatile ("movl %0, %%cr3" : : "r"(v) : "memory");
}

static inline u32 read_cr4(void)
{
	u32 v;

	__asm__ volatile ("movl %%cr4, %0" : "=r"(v));
	return v;
}

static inline void write_cr4(u32 v)
{
	__asm__ volatile ("movl %0, %%cr4" : : "r"(v) : "memory");
}

static inline void invlpg(u32 addr)
{
	__asm__ volatile ("invlpg (%0)" : : "r"(addr) : "memory");
}

#endif /* _HIX_IO_H */
