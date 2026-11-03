/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * hix runs on a single CPU, so a spinlock is "disable local interrupts".
 * The API mirrors Linux so the code keeps its shape if SMP ever arrives.
 *
 * Locking discipline (see docs/ARCHITECTURE.md):
 *  - short critical sections, never sleeping:    spin_lock_irqsave()
 *  - long or sleeping critical sections:         struct mutex
 */
#ifndef _HIX_SPINLOCK_H
#define _HIX_SPINLOCK_H

#include <asm/irqflags.h>

typedef struct {
	int locked;	/* debug only: catches recursive locking */
} spinlock_t;

#define __SPIN_LOCK_UNLOCKED	{ 0 }
#define DEFINE_SPINLOCK(x)	spinlock_t x = __SPIN_LOCK_UNLOCKED

static inline void spin_lock_init(spinlock_t *lock)
{
	lock->locked = 0;
}

#define spin_lock_irqsave(lock, flags)		\
	do {					\
		local_irq_save(flags);		\
		(lock)->locked++;		\
	} while (0)

#define spin_unlock_irqrestore(lock, flags)	\
	do {					\
		(lock)->locked--;		\
		local_irq_restore(flags);	\
	} while (0)

#endif /* _HIX_SPINLOCK_H */
