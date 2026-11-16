/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Fast memory primitives for i386: string instructions with a REP prefix
 * move a word per step (and a whole cache line per step on newer cores),
 * where a C loop moves a byte. Everything the display does, every fill
 * and blit, ends up here.
 *
 * Kept in a header so the host test can include it directly and compare it
 * with a plain loop (tests/test_fastmem.c, built as a 32-bit program).
 */
#ifndef _LIB_FASTMEM_H
#define _LIB_FASTMEM_H

#include <hix/types.h>

static inline void *fast_memset(void *s, int c, size_t n)
{
	void *d = s;
	size_t words = n >> 2;
	size_t tail = n & 3;
	unsigned long v = (unsigned char)c * 0x01010101UL;

	__asm__ volatile("cld; rep stosl"
			 : "+D"(d), "+c"(words) : "a"(v) : "memory");
	__asm__ volatile("rep stosb"
			 : "+D"(d), "+c"(tail) : "a"(v) : "memory");
	return s;
}

static inline void fast_memset32(u32 *p, u32 v, size_t n)
{
	__asm__ volatile("cld; rep stosl"
			 : "+D"(p), "+c"(n) : "a"(v) : "memory");
}

static inline void *fast_memcpy(void *dst, const void *src, size_t n)
{
	void *d = dst;
	const void *s = src;
	size_t words = n >> 2;
	size_t tail = n & 3;

	__asm__ volatile("cld; rep movsl"
			 : "+D"(d), "+S"(s), "+c"(words) : : "memory");
	__asm__ volatile("rep movsb"
			 : "+D"(d), "+S"(s), "+c"(tail) : : "memory");
	return dst;
}

/* copies correctly when the ranges overlap */
static inline void *fast_memmove(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	if (d <= s || d >= s + n)
		return fast_memcpy(dst, src, n);
	/* the destination starts inside the source: copy from the end */
	d += n;
	s += n;
	while (n >= 4) {
		u32 w;

		d -= 4;
		s -= 4;
		n -= 4;
		__builtin_memcpy(&w, s, 4);	/* one unaligned load */
		__builtin_memcpy(d, &w, 4);	/* and one store */
	}
	while (n--)
		*--d = *--s;
	return dst;
}

#endif /* _LIB_FASTMEM_H */
