/* SPDX-License-Identifier: GPL-2.0-only */
/* aether: wait queues and mutexes (the sleeping primitives). */
#ifndef _AETHER_WAITQ_H
#define _AETHER_WAITQ_H

#include <hix/list.h>
#include <hix/spinlock.h>

struct waitq {
	struct list_head waiters;
};

#define WAITQ_INIT(name)	{ LIST_HEAD_INIT((name).waiters) }
#define DECLARE_WAITQ(name)	struct waitq name = WAITQ_INIT(name)

static inline void waitq_init(struct waitq *wq)
{
	INIT_LIST_HEAD(&wq->waiters);
}

/*
 * Sleep on @wq until woken or until @timeout ticks passed (0 = forever).
 * Returns 0 when woken, -ETIMEDOUT on timeout.
 *
 * MUST be called with interrupts disabled. That is what makes
 * "test condition, then sleep" atomic against a wakeup from an interrupt:
 *
 *	local_irq_save(flags);
 *	while (!condition)
 *		if (waitq_wait_irq(&wq, timeout))
 *			break;
 *	local_irq_restore(flags);
 */
int waitq_wait_irq(struct waitq *wq, unsigned long timeout);

void waitq_wake_one(struct waitq *wq);
void waitq_wake_all(struct waitq *wq);

/* a plain non-recursive sleeping lock */
struct mutex {
	int locked;
	struct waitq wq;
};

#define MUTEX_INIT(name)	{ 0, WAITQ_INIT((name).wq) }
#define DEFINE_MUTEX(name)	struct mutex name = MUTEX_INIT(name)

static inline void mutex_init(struct mutex *m)
{
	m->locked = 0;
	waitq_init(&m->wq);
}

void mutex_lock(struct mutex *m);
void mutex_unlock(struct mutex *m);

#endif /* _AETHER_WAITQ_H */
