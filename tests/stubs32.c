// SPDX-License-Identifier: GPL-2.0-only
/*
 * Runtime for test_sched: the real scheduler, IPC and wait queues run as a
 * 32-bit Linux process (no libc), with the real ctx_switch from
 * arch/x86/switch.asm. Linux int 0x80 stands in for the console.
 */
#include <stdarg.h>
#include <hix/kernel.h>
#include <aether/vm.h>
#include "t32.h"

char boot_stack_bottom[16];
volatile int need_resched;
char __kernel_end[1];

static long sc3(long n, long a, long b, long c)
{
	long r;

	__asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b),
			 "d"(c) : "memory");
	return r;
}

void t_exit(int code)
{
	sc3(1, code, 0, 0);
	for (;;)
		;
}

void t_print(const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	sc3(4, 1, (long)buf, n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1);
}

int tests_run, tests_failed;

void t_check(int ok, const char *expr, int line)
{
	tests_run++;
	if (!ok) {
		tests_failed++;
		t_print("  FAIL line %d: %s\n", line, expr);
	}
}

void panic(const char *fmt, ...)
{
	char buf[200];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	t_print("  PANIC: %s\n", buf);
	t_exit(2);
	for (;;)
		;
}

int printk(const char *fmt, ...)
{
	(void)fmt;
	return 0;
}

/* compiler-emitted calls: plain names, forwarding to our renamed ones */
#undef memcpy
#undef memset
void *memcpy(void *d, const void *s, size_t n) { return hix_memcpy(d, s, n); }
void *memset(void *d, int c, size_t n) { return hix_memset(d, c, n); }

/* thread stacks come from a static arena; frees are only counted */
static char stack_arena[64 * 5 * 4096] __attribute__((aligned(4096)));
static unsigned int stack_next;
int stacks_freed;

void *vm_stack_alloc(unsigned int pages)
{
	void *p = stack_arena + (size_t)stack_next * 4096 + 4096;

	stack_next += pages + 1;
	return p;
}

void vm_stack_free(void *base, unsigned int pages)
{
	(void)base;
	(void)pages;
	stacks_freed++;
}

bool vm_addr_is_mapped(unsigned long a) { (void)a; return true; }
