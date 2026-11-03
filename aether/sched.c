// SPDX-License-Identifier: GPL-2.0-only
/*
 * aether: threads and the scheduler.
 *
 * Round-robin with a fixed time slice, one run queue, single CPU. Every
 * thread owns a kernel stack (guarded by an unmapped page); a switch just
 * swaps stack pointers in ctx_switch(). The boot context becomes the idle
 * thread, which runs when nothing else can.
 *
 * Rules:
 *  - schedule() runs with interrupts disabled and returns with them
 *    disabled; callers restore their own flags afterwards.
 *  - the timer only sets need_resched; the actual switch happens on the way
 *    out of the interrupt (arch/x86/traps.c) or when a thread blocks.
 */
#define pr_fmt(fmt) "aether: sched: " fmt

#include <hix/kernel.h>
#include <hix/spinlock.h>
#include <asm/trap.h>
#include <aether/kalloc.h>
#include <aether/sched.h>
#include <aether/vm.h>
#include <aether/waitq.h>

#define KSTACK_PAGES	4			/* 16 KiB of kernel stack */

extern char boot_stack_bottom[];
extern void ctx_switch(unsigned long *old_sp, unsigned long new_sp);
extern void thread_trampoline(void);

static LIST_HEAD(runq);
static LIST_HEAD(dead_list);
static LIST_HEAD(all_threads);
static DEFINE_SPINLOCK(thread_lock);

static struct thread *current_thread;
static struct thread *idle_thread;
static int next_tid;

struct thread *thread_current(void)
{
	return current_thread;
}

const char *thread_state_name(enum thread_state state)
{
	switch (state) {
	case THREAD_RUNNING:
		return "running";
	case THREAD_READY:
		return "ready";
	case THREAD_WAITING:
		return "waiting";
	case THREAD_DEAD:
		return "dead";
	}
	return "?";
}

static void thread_link(struct thread *t, struct task *task, const char *name)
{
	strlcpy(t->name, name, sizeof(t->name));
	t->task = task;
	t->slice = TIME_SLICE;
	INIT_LIST_HEAD(&t->run_node);
	INIT_LIST_HEAD(&t->wait_node);
	INIT_LIST_HEAD(&t->timer_node);

	list_add_tail(&t->task_node, &task->threads);
	task->nthreads++;
	list_add_tail(&t->all_node, &all_threads);
	t->tid = next_tid++;
}

void sched_init(void)
{
	struct thread *t = kzalloc(sizeof(*t));

	BUG_ON(!t);
	thread_link(t, kernel_task, "idle");
	t->state = THREAD_RUNNING;
	t->stack = boot_stack_bottom;		/* not owned: never freed */
	t->stack_pages = 4;
	idle_thread = t;
	current_thread = t;
}

/* a thread that is runnable goes to the tail of the run queue */
void thread_ready(struct thread *t)
{
	t->state = THREAD_READY;
	list_add_tail(&t->run_node, &runq);
	/* the idle thread must not keep the CPU while work is waiting */
	if (current_thread == idle_thread)
		need_resched = 1;
}

/* free the stacks of dead threads; never the one we are running on */
static void reap_dead(void)
{
	struct thread *t, *n;

	list_for_each_entry_safe(t, n, &dead_list, run_node) {
		if (t == current_thread)
			continue;
		list_del(&t->run_node);
		list_del(&t->all_node);
		vm_stack_free(t->stack, t->stack_pages);
		kfree(t);
	}
}

void schedule(void)
{
	struct thread *prev = current_thread;
	struct thread *next;

	BUG_ON(!irqs_disabled());
	need_resched = 0;

	if (prev->state == THREAD_RUNNING) {
		prev->state = THREAD_READY;
		if (prev != idle_thread)
			list_add_tail(&prev->run_node, &runq);
	}

	if (list_empty(&runq)) {
		next = idle_thread;
	} else {
		next = list_first_entry(&runq, struct thread, run_node);
		list_del_init(&next->run_node);
	}
	next->state = THREAD_RUNNING;
	next->slice = TIME_SLICE;

	if (next != prev) {
		current_thread = next;
		ctx_switch(&prev->sp, next->sp);
		/* we are prev again, some time later */
		reap_dead();
	}
}

void sched_tick(void)
{
	waitq_tick();

	if (!current_thread)
		return;
	current_thread->cpu_ticks++;

	if (current_thread == idle_thread) {
		if (!list_empty(&runq))
			need_resched = 1;
		return;
	}
	if (--current_thread->slice <= 0) {
		if (!list_empty(&runq))
			need_resched = 1;
		else
			current_thread->slice = TIME_SLICE;
	}
}

void sched_idle_loop(void)
{
	for (;;) {
		local_irq_disable();
		reap_dead();
		if (!list_empty(&runq))
			schedule();
		/* "sti; hlt" is atomic: no wakeup can slip in between */
		__asm__ volatile("sti; hlt" : : : "memory");
	}
}

struct thread *thread_create(struct task *task, const char *name,
			     void (*fn)(void *), void *arg)
{
	struct thread *t = kzalloc(sizeof(*t));
	unsigned long flags;
	u32 *sp;

	if (!t)
		return NULL;
	t->stack = vm_stack_alloc(KSTACK_PAGES);
	if (!t->stack) {
		kfree(t);
		return NULL;
	}
	t->stack_pages = KSTACK_PAGES;

	/*
	 * Fake the frame ctx_switch() will pop: edi esi ebx ebp, then the
	 * return address. thread_trampoline() calls ebx(esi).
	 */
	sp = (u32 *)((u8 *)t->stack + KSTACK_PAGES * PAGE_SIZE);
	*--sp = (u32)(uintptr_t)thread_trampoline;
	*--sp = 0;				/* ebp: end of frame chain */
	*--sp = (u32)(uintptr_t)fn;		/* ebx */
	*--sp = (u32)(uintptr_t)arg;		/* esi */
	*--sp = 0;				/* edi */
	t->sp = (unsigned long)sp;

	spin_lock_irqsave(&thread_lock, flags);
	thread_link(t, task, name);
	thread_ready(t);
	spin_unlock_irqrestore(&thread_lock, flags);
	return t;
}

void thread_exit(void)
{
	struct thread *t = current_thread;

	local_irq_disable();
	BUG_ON(t == idle_thread);

	list_del_init(&t->task_node);
	t->task->nthreads--;
	t->state = THREAD_DEAD;
	list_add_tail(&t->run_node, &dead_list);

	task_thread_gone(t->task);	/* may tear the whole task down */

	schedule();
	panic("dead thread %s was scheduled again", t->name);
}

/* where thread_trampoline lands when a thread function returns */
void thread_finish(void)
{
	thread_exit();
}

void thread_yield(void)
{
	unsigned long flags;

	local_irq_save(flags);
	schedule();
	local_irq_restore(flags);
}

void thread_sleep(unsigned long ticks)
{
	unsigned long flags;
	DECLARE_WAITQ(wq);			/* nobody ever wakes it */

	if (!ticks) {
		thread_yield();
		return;
	}
	local_irq_save(flags);
	waitq_wait_irq(&wq, ticks);
	local_irq_restore(flags);
}

int thread_snapshot(struct thread_info *out, int max)
{
	unsigned long flags;
	struct thread *t;
	int n = 0;

	spin_lock_irqsave(&thread_lock, flags);
	list_for_each_entry(t, &all_threads, all_node) {
		if (n >= max)
			break;
		if (t->state == THREAD_DEAD)
			continue;
		out[n].tid = t->tid;
		out[n].task_id = t->task->id;
		strlcpy(out[n].name, t->name, sizeof(out[n].name));
		out[n].state = t->state;
		out[n].cpu_ticks = t->cpu_ticks;
		n++;
	}
	spin_unlock_irqrestore(&thread_lock, flags);
	return n;
}
