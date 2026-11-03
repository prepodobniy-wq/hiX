// SPDX-License-Identifier: GPL-2.0-only
/* Stand-ins for the parts of the kernel that only exist on real hardware. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "hosttest.h"
#include <hix/kernel.h>
#include <hix/console.h>
#include <aether/waitq.h>
#include <aether/task.h>

int tests_run, tests_failed;
volatile unsigned long jiffies;
char __kernel_end[1];
volatile int need_resched;

void panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	printf("  PANIC: ");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
	abort();
}

int printk(const char *fmt, ...)
{
	(void)fmt;
	return 0;
}

/* "sleeping" in a single-threaded test means the wait timed out */
int waitq_wait_irq(struct waitq *wq, unsigned long timeout)
{
	(void)wq;
	(void)timeout;
	return -ETIMEDOUT;
}

void waitq_wake_one(struct waitq *wq) { (void)wq; }
void waitq_wake_all(struct waitq *wq) { (void)wq; }
void mutex_lock(struct mutex *m) { m->locked = 1; }
void mutex_unlock(struct mutex *m) { m->locked = 0; }

void register_console(struct console *con) { (void)con; }
