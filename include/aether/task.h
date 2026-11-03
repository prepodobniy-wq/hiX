/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * aether: tasks and threads.
 *
 * A task is a resource container (port namespace, address space
 * later on) and a thread is the unit of execution inside it. The qubix layer
 * (qubix) puts a struct proc on top of a task; aether knows nothing about it
 * except an opaque pointer and an exit callback.
 */
#ifndef _AETHER_TASK_H
#define _AETHER_TASK_H

#include <hix/types.h>
#include <hix/list.h>
#include <aether/ipc.h>
#include <aether/waitq.h>

#define TASK_NAME_LEN	16

enum thread_state {
	THREAD_RUNNING,
	THREAD_READY,
	THREAD_WAITING,
	THREAD_DEAD,
};

struct task;

struct thread {
	unsigned long sp;		/* saved kernel stack pointer */
	int tid;
	char name[TASK_NAME_LEN];
	enum thread_state state;
	struct task *task;

	void *stack;			/* usable base, NULL for the boot thread */
	unsigned int stack_pages;
	int slice;			/* ticks left of the time slice */
	unsigned long cpu_ticks;	/* ticks spent running */

	struct list_head run_node;	/* run queue or dead list */
	struct list_head task_node;	/* task->threads */
	struct list_head all_node;	/* all threads, for ps */

	struct waitq *waiting_on;	/* non-NULL while blocked */
	struct list_head wait_node;	/* waitq->waiters */
	bool timed;			/* has a timeout armed */
	unsigned long wake_at;
	struct list_head timer_node;	/* timed waiters */
	int wait_result;
};

struct task {
	int id;
	char name[TASK_NAME_LEN];
	struct list_head threads;
	int nthreads;
	bool terminated;		/* all threads gone */
	struct ipc_space space;
	struct list_head node;		/* all tasks */

	void *qproc;			/* owned by qubix */
	void (*on_exit)(struct task *task);	/* last thread exited */
};

extern struct task *kernel_task;

void task_init(void);			/* creates kernel_task */
struct task *task_create(const char *name);
void task_destroy(struct task *task);	/* only after termination */
void task_thread_gone(struct task *task);	/* thread_exit() calls this */

struct thread *thread_create(struct task *task, const char *name,
			     void (*fn)(void *), void *arg);
void thread_exit(void) __attribute__((noreturn));
void thread_yield(void);
void thread_sleep(unsigned long ticks);
struct thread *thread_current(void);	/* NULL before the scheduler is up */

/* snapshots for ps(1) style tools; nothing here exposes live pointers */
struct task_info {
	int id;
	char name[TASK_NAME_LEN];
	int nthreads;
	unsigned int nports;
};

struct thread_info {
	int tid;
	int task_id;
	char name[TASK_NAME_LEN];
	enum thread_state state;
	unsigned long cpu_ticks;
};

int task_snapshot(struct task_info *out, int max);
int thread_snapshot(struct thread_info *out, int max);
const char *thread_state_name(enum thread_state state);

#endif /* _AETHER_TASK_H */
