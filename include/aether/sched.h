/* SPDX-License-Identifier: GPL-2.0-only */
/* aether: scheduler and clock. */
#ifndef _AETHER_SCHED_H
#define _AETHER_SCHED_H

#include <hix/types.h>
#include <aether/task.h>

#define TIME_SLICE	5		/* ticks (50 ms at 100 Hz) */

void sched_init(void);			/* the boot context becomes "idle" */

/* Pick the next thread and switch to it. Interrupts must be disabled; they
 * are still disabled when schedule() returns (for the resumed thread). */
void schedule(void);

/* Idle loop of the boot thread; never returns. */
void sched_idle_loop(void) __attribute__((noreturn));

/* make a blocked thread runnable again; interrupts must be disabled */
void thread_ready(struct thread *t);

/* called by the timer interrupt */
void sched_tick(void);

/* expire timed sleepers; part of sched_tick() */
void waitq_tick(void);

/* where thread_trampoline (arch/x86/switch.asm) lands */
void thread_finish(void) __attribute__((noreturn));

/* clock.c */
void clock_tick(void);			/* jiffies++ and sched_tick() */
unsigned long clock_uptime_ms(void);

#endif /* _AETHER_SCHED_H */
