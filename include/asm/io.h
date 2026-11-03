/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_IO_H
#define _ASM_IO_H

#include <hix/types.h>

#ifdef HIX_HOST_TEST

/* unit tests run on the host: port I/O is a no-op */
static inline void outb(u16 port, u8 val) { (void)port; (void)val; }
static inline void outw(u16 port, u16 val) { (void)port; (void)val; }
static inline u8 inb(u16 port) { (void)port; return 0; }
static inline u16 inw(u16 port) { (void)port; return 0; }
static inline void outl(u16 port, u32 val) { (void)port; (void)val; }
static inline u32 inl(u16 port) { (void)port; return 0; }

#else

static inline void outb(u16 port, u8 val)
{
	__asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(u16 port, u16 val)
{
	__asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline u8 inb(u16 port)
{
	u8 val;

	__asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}

static inline u16 inw(u16 port)
{
	u16 val;

	__asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}

static inline void outl(u16 port, u32 val)
{
	__asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline u32 inl(u16 port)
{
	u32 val;

	__asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}

#endif /* HIX_HOST_TEST */

/* a tiny delay for slow legacy devices (PIC, PS/2) */
static inline void io_wait(void)
{
	outb(0x80, 0);
}

#endif /* _ASM_IO_H */
