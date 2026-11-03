/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _HIX_COMPILER_H
#define _HIX_COMPILER_H

#define __packed		__attribute__((packed))
#define __aligned(x)		__attribute__((aligned(x)))
#define __noreturn		__attribute__((noreturn))
#define __used			__attribute__((used))
#define __unused		__attribute__((unused))
#define __section(s)		__attribute__((section(s)))
#define __printf(a, b)		__attribute__((format(printf, a, b)))

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

#define barrier()	__asm__ volatile("" : : : "memory")

#define ARRAY_SIZE(a)	(sizeof(a) / sizeof((a)[0]))

#define BIT(n)		(1UL << (n))

/* each use gets its own temporaries, so min() and max() nest */
#define __PASTE(a, b)		a##b
#define __UNIQ(p, n)		__PASTE(p, n)
#define __cmp_once(op, x, y, ux, uy) ({ \
		__typeof__(x) ux = (x); __typeof__(y) uy = (y); \
		ux op uy ? ux : uy; })
#define __cmp(op, x, y)		__cmp_once(op, x, y, __UNIQ(_x, __COUNTER__), \
					   __UNIQ(_y, __COUNTER__))
#define min(a, b)		__cmp(<, a, b)
#define max(a, b)		__cmp(>, a, b)
#ifdef HIX_HOST_TEST		/* host builds use the C library's abs() */
int abs(int);
#else
#define abs(x)			({ __typeof__(x) _v = (x); _v < 0 ? -_v : _v; })
#endif

#define ALIGN(x, a)		(((x) + ((a) - 1)) & ~((__typeof__(x))(a) - 1))
#define ALIGN_DOWN(x, a)	((x) & ~((__typeof__(x))(a) - 1))
#define IS_ALIGNED(x, a)	(((x) & ((__typeof__(x))(a) - 1)) == 0)

#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

#define DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))

#endif /* _HIX_COMPILER_H */
