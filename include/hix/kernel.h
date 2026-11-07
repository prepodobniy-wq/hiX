/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _HIX_KERNEL_H
#define _HIX_KERNEL_H

#include <hix/types.h>
#include <hix/compiler.h>
#include <hix/errno.h>
#include <hix/printk.h>
#include <hix/string.h>

struct pt_regs;

void panic(const char *fmt, ...) __noreturn __printf(1, 2);

/* lib/cxxrt.c: constructors of global C++ objects */
void cxx_init(void);
void panic_regs(const struct pt_regs *regs, const char *fmt, ...)
	__noreturn __printf(2, 3);

#define BUG()								\
	do {								\
		panic("BUG at %s:%d (%s)", __FILE__, __LINE__, __func__); \
	} while (0)

#define BUG_ON(cond)	do { if (unlikely(cond)) BUG(); } while (0)

#define WARN_ON(cond) ({						\
	int __ret = !!(cond);						\
	if (unlikely(__ret))						\
		pr_warn("WARNING at %s:%d (%s)\n", __FILE__, __LINE__,	\
			__func__);					\
	unlikely(__ret);						\
})

/* ticks of the system timer, see aether/clock.h */
#define HZ	100
extern volatile unsigned long jiffies;

#define time_after(a, b)	((long)((b) - (a)) < 0)
#define time_after_eq(a, b)	((long)((a) - (b)) >= 0)

#define msecs_to_jiffies(ms)	(((ms) * HZ + 999) / 1000)

#endif /* _HIX_KERNEL_H */
