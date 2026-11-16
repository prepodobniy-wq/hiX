// SPDX-License-Identifier: GPL-2.0-only
/*
 * The i386 string-instruction memory primitives (lib/fastmem.h) against
 * plain loops, for every size up to 300 and every alignment of source and
 * destination, overlapping ranges included. A freestanding 32-bit Linux
 * program: the asm is i386, and it needs no libc.
 */
#include <hix/types.h>
#include "../lib/fastmem.h"

static int run, failed;
#define CHECK(c) do { run++; if (!(c)) failed++; } while (0)

static long sys3(long n, long a, long b, long c)
{
	long r;

	__asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b),
			 "d"(c) : "memory");
	return r;
}

static void say(const char *s)
{
	int n = 0;

	while (s[n])
		n++;
	sys3(4, 1, (long)s, n);
}

static void say_num(int v)
{
	char b[16];
	int i = 15;

	b[i] = 0;
	do {
		b[--i] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	say(b + i);
}

/* references: plain byte loops (this file is built with no loop-to-call magic) */
static void ref_copy(unsigned char *d, const unsigned char *s, int n)
{
	int i;

	for (i = 0; i < n; i++)
		d[i] = s[i];
}

static void ref_move(unsigned char *d, const unsigned char *s, int n)
{
	int i;

	if (d < s)
		for (i = 0; i < n; i++)
			d[i] = s[i];
	else
		for (i = n - 1; i >= 0; i--)
			d[i] = s[i];
}

static int same(const unsigned char *x, const unsigned char *y, int n)
{
	int i;

	for (i = 0; i < n; i++)
		if (x[i] != y[i])
			return 0;
	return 1;
}

static unsigned char a[700], b[700], ref[700];

static void fill(unsigned char *p, int n, int seed)
{
	int i;

	for (i = 0; i < n; i++)
		p[i] = (unsigned char)(i * 31 + seed);
}

static void tests(void)
{
	int n, so, dof, i;

	for (n = 0; n <= 300; n++) {
		for (so = 0; so < 4; so++) {
			for (dof = 0; dof < 4; dof++) {
				fill(a, 700, 1);
				fill(b, 700, 77);
				ref_copy(ref, b, 700);
				ref_copy(ref + 8 + dof, a + 8 + so, n);
				fast_memcpy(b + 8 + dof, a + 8 + so, (size_t)n);
				CHECK(same(b, ref, 700));
			}
		}
	}
	for (n = 0; n <= 300; n++) {
		for (dof = 0; dof < 4; dof++) {
			fill(a, 700, 5);
			ref_copy(ref, a, 700);
			for (i = 0; i < n; i++)
				ref[4 + dof + i] = 0xA7;
			fast_memset(a + 4 + dof, 0xA7, (size_t)n);
			CHECK(same(a, ref, 700));
		}
	}
	for (n = 0; n <= 200; n += 3) {
		int shift;

		for (shift = -9; shift <= 9; shift++) {
			fill(a, 700, 9);
			ref_copy(ref, a, 700);
			ref_move(ref + 100 + shift, ref + 100, n);
			fast_memmove(a + 100 + shift, a + 100, (size_t)n);
			CHECK(same(a, ref, 700));
		}
	}
	{
		static u32 px[300];

		for (n = 0; n <= 100; n++) {
			for (i = 0; i < 300; i++)
				px[i] = 0;
			fast_memset32(px + 3, 0xDEADBEEFu, (size_t)n);
			for (i = 0; i < 300; i++)
				CHECK(px[i] == (i >= 3 && i < 3 + n ? 0xDEADBEEFu : 0));
		}
	}
}

void _start(void)
{
	say("test_fastmem\n");
	tests();
	say("  ");
	say_num(run);
	say(" checks, ");
	say_num(failed);
	say(" failed\n");
	sys3(1, failed ? 1 : 0, 0, 0);
	for (;;)
		;
}
