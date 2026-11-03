// SPDX-License-Identifier: GPL-2.0-only
/* aether: wait queues, timeouts and mutexes. */

#include <hix/kernel.h>
#include <hix/spinlock.h>
#include <aether/sched.h>
#include <aether/waitq.h>

/* threads sleeping with a timeout; scanned on every timer tick */
static LIST_HEAD(timed_waiters);

/* make @t runnable, dropping it from its wait queue; interrupts off */
static void wake_thread(struct thread *t, int result)
{
	list_del_init(&t->wait_node);
	t->waiting_on = NULL;
	if (t->timed) {
		list_del_init(&t->timer_node);
		t->timed = false;
	}
	t->wait_result = result;
	thread_ready(t);
}

int waitq_wait_irq(struct waitq *wq, unsigned long timeout)
{
	struct thread *t = thread_current();

	BUG_ON(!t);
	BUG_ON(!irqs_disabled());

	t->wait_result = 0;
	t->waiting_on = wq;
	list_add_tail(&t->wait_node, &wq->waiters);
	if (timeout) {
		t->timed = true;
		t->wake_at = jiffies + timeout;
		list_add_tail(&t->timer_node, &timed_waiters);
	}
	t->state = THREAD_WAITING;
	schedule();
	return t->wait_result;
}

void waitq_wake_one(struct waitq *wq)
{
	unsigned long flags;

	local_irq_save(flags);
	if (!list_empty(&wq->waiters))
		wake_thread(list_first_entry(&wq->waiters, struct thread,
					     wait_node), 0);
	local_irq_restore(flags);
}

void waitq_wake_all(struct waitq *wq)
{
	unsigned long flags;

	local_irq_save(flags);
	while (!list_empty(&wq->waiters))
		wake_thread(list_first_entry(&wq->waiters, struct thread,
					     wait_node), 0);
	local_irq_restore(flags);
}

/* timer interrupt: expire sleepers whose time has come */
void waitq_tick(void)
{
	struct thread *t, *n;

	list_for_each_entry_safe(t, n, &timed_waiters, timer_node) {
		if (time_after_eq(jiffies, t->wake_at))
			wake_thread(t, -ETIMEDOUT);
	}
}

void mutex_lock(struct mutex *m)
{
	unsigned long flags;

	local_irq_save(flags);
	while (m->locked)
		waitq_wait_irq(&m->wq, 0);
	m->locked = 1;
	local_irq_restore(flags);
}

void mutex_unlock(struct mutex *m)
{
	unsigned long flags;

	local_irq_save(flags);
	BUG_ON(!m->locked);
	m->locked = 0;
	waitq_wake_one(&m->wq);
	local_irq_restore(flags);
}
