// SPDX-License-Identifier: GPL-2.0-only
/* aether: tasks. Threads live in sched.c. */
#define pr_fmt(fmt) "aether: task: " fmt

#include <hix/kernel.h>
#include <hix/spinlock.h>
#include <aether/kalloc.h>
#include <aether/task.h>

struct task *kernel_task;

static LIST_HEAD(all_tasks);
static DEFINE_SPINLOCK(task_lock);
static int next_task_id;

struct task *task_create(const char *name)
{
	struct task *task = kzalloc(sizeof(*task));
	unsigned long flags;

	if (!task)
		return NULL;

	strlcpy(task->name, name, sizeof(task->name));
	INIT_LIST_HEAD(&task->threads);
	ipc_space_init(&task->space);

	spin_lock_irqsave(&task_lock, flags);
	task->id = next_task_id++;
	list_add_tail(&task->node, &all_tasks);
	spin_unlock_irqrestore(&task_lock, flags);
	return task;
}

void task_destroy(struct task *task)
{
	unsigned long flags;

	BUG_ON(!task->terminated);
	spin_lock_irqsave(&task_lock, flags);
	list_del(&task->node);
	spin_unlock_irqrestore(&task_lock, flags);
	kfree(task);
}

void task_init(void)
{
	ipc_init();
	kernel_task = task_create("kernel_task");
	BUG_ON(!kernel_task);
}

/* called by thread_exit() after @task lost a thread, interrupts off */
void task_thread_gone(struct task *task)
{
	if (task->nthreads || task == kernel_task)
		return;

	task->terminated = true;
	ipc_space_destroy(&task->space);	/* kills the ports it owned */
	if (task->on_exit)
		task->on_exit(task);
}

int task_snapshot(struct task_info *out, int max)
{
	unsigned long flags;
	struct task *task;
	int n = 0;

	spin_lock_irqsave(&task_lock, flags);
	list_for_each_entry(task, &all_tasks, node) {
		if (n >= max)
			break;
		out[n].id = task->id;
		strlcpy(out[n].name, task->name, sizeof(out[n].name));
		out[n].nthreads = task->nthreads;
		out[n].nports = ipc_space_count(&task->space);
		n++;
	}
	spin_unlock_irqrestore(&task_lock, flags);
	return n;
}
