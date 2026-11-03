// SPDX-License-Identifier: GPL-2.0-only
/*
 * qubix: process management on top of aether tasks.
 *
 * spawn:  new task + proc + first thread running a built-in program
 * exit:   close files, the thread dies, the task's last-thread hook turns
 *         the proc into a zombie and wakes the parent
 * wait4:  the parent collects the zombie and destroys proc and task
 */
#define pr_fmt(fmt) "qubix: proc: " fmt

#include <hix/kernel.h>
#include <hix/spinlock.h>
#include <aether/kalloc.h>
#include <aether/sched.h>
#include <qubix/libq.h>
#include <qubix/proc.h>
#include <qubix/tty.h>

static LIST_HEAD(all_procs);
static DEFINE_SPINLOCK(proc_lock);
static pid_t next_pid = 1;
static struct proc kernel_proc;

struct proc *proc_current(void)
{
	struct thread *t = thread_current();

	if (t && t->task && t->task->qproc)
		return t->task->qproc;
	return &kernel_proc;
}

void proc_init(void)
{
	kernel_proc.pid = 0;
	strlcpy(kernel_proc.comm, "kernel", sizeof(kernel_proc.comm));
	INIT_LIST_HEAD(&kernel_proc.node);
	waitq_init(&kernel_proc.child_wq);
	kernel_task->qproc = &kernel_proc;
}

/* ---- file descriptors ------------------------------------------------ */

int proc_fd_alloc(struct proc *p, struct file *f)
{
	int fd;

	for (fd = 0; fd < NOFILE; fd++) {
		if (!p->fds[fd]) {
			p->fds[fd] = f;
			return fd;
		}
	}
	return -EMFILE;
}

struct file *proc_fd_get(struct proc *p, int fd)
{
	return (fd >= 0 && fd < NOFILE) ? p->fds[fd] : NULL;
}

int proc_fd_close(struct proc *p, int fd)
{
	struct file *f = proc_fd_get(p, fd);

	if (!f)
		return -EBADF;
	p->fds[fd] = NULL;
	vfs_close(f);
	return 0;
}

/* ---- life cycle ------------------------------------------------------ */

/* aether hook: the last thread of @task is gone (interrupts off!) */
static void proc_task_dead(struct task *task)
{
	struct proc *p = task->qproc, *c;

	p->state = PROC_ZOMBIE;

	/* orphans are adopted by pid 1 */
	list_for_each_entry(c, &all_procs, node) {
		if (c->ppid == p->pid && c != p) {
			c->ppid = 1;
			if (c->state == PROC_ZOMBIE) {
				struct proc *init;

				list_for_each_entry(init, &all_procs, node)
					if (init->pid == 1)
						waitq_wake_all(&init->child_wq);
			}
		}
	}
	list_for_each_entry(c, &all_procs, node)
		if (c->pid == p->ppid)
			waitq_wake_all(&c->child_wq);
	waitq_wake_all(&kernel_proc.child_wq);
}

void proc_exit(int status)
{
	struct proc *p = proc_current();
	int fd;

	p->exit_status = status;
	qubix_display_release(p);	/* a program that dies gives the screen back */
	tty_set_raw(false);		/* ... and the keyboard */
	for (fd = 0; fd < NOFILE; fd++)
		proc_fd_close(p, fd);
	thread_exit();
}

static const struct qprog *find_prog(const char *name)
{
	int i;

	for (i = 0; i < qprogs_count; i++)
		if (!strcmp(qprogs[i].name, name))
			return &qprogs[i];
	return NULL;
}

/* first code of every process thread */
static void proc_entry(void *arg)
{
	struct proc *p = arg;
	const struct qprog *prog = find_prog(p->comm);
	char *argv[PROC_ARGS_MAX + 1];
	char *s = p->cmdline;
	int argc = 0;

	while (*s && argc < PROC_ARGS_MAX) {
		while (*s == ' ')
			*s++ = '\0';
		if (!*s)
			break;
		argv[argc++] = s;
		while (*s && *s != ' ')
			s++;
	}
	argv[argc] = NULL;

	/* from here on the program only talks to the kernel via int 0x80 */
	q_exit(prog->main(argc, argv));
}

pid_t proc_spawn(const char *cmdline)
{
	struct proc *parent = proc_current();
	struct proc *p;
	struct task *task;
	char name[TASK_NAME_LEN];
	const struct qprog *prog;
	size_t n = 0;
	unsigned long flags;
	int fd;

	while (cmdline[n] && cmdline[n] != ' ' && n < sizeof(name) - 1) {
		name[n] = cmdline[n];
		n++;
	}
	name[n] = '\0';
	prog = find_prog(name);
	if (!prog)
		return -ENOENT;

	p = kzalloc(sizeof(*p));
	if (!p)
		return -ENOMEM;
	p->cmdline = kmalloc(strlen(cmdline) + 1);
	task = task_create(name);
	if (!p->cmdline || !task) {
		kfree(p->cmdline);
		kfree(p);
		if (task) {
			task->terminated = true;
			task_destroy(task);
		}
		return -ENOMEM;
	}
	memcpy(p->cmdline, cmdline, strlen(cmdline) + 1);
	strlcpy(p->comm, name, sizeof(p->comm));
	p->task = task;
	p->ppid = parent->pid;
	waitq_init(&p->child_wq);
	task->qproc = p;
	task->on_exit = proc_task_dead;

	/* stdin, stdout, stderr: the console */
	for (fd = 0; fd < 3; fd++) {
		struct file *f;

		if (vfs_open("/dev/console", O_RDWR, &f) == 0)
			proc_fd_alloc(p, f);
	}

	spin_lock_irqsave(&proc_lock, flags);
	p->pid = next_pid++;
	list_add_tail(&p->node, &all_procs);
	spin_unlock_irqrestore(&proc_lock, flags);

	if (!thread_create(task, name, proc_entry, p)) {
		/* nothing ran yet: undo */
		spin_lock_irqsave(&proc_lock, flags);
		list_del(&p->node);
		spin_unlock_irqrestore(&proc_lock, flags);
		for (fd = 0; fd < NOFILE; fd++)
			proc_fd_close(p, fd);
		task->terminated = true;
		task_destroy(task);
		kfree(p->cmdline);
		kfree(p);
		return -ENOMEM;
	}
	return p->pid;
}

long proc_wait(pid_t pid, int *status)
{
	struct proc *me = proc_current();
	unsigned long flags;
	long ret;

	local_irq_save(flags);
	for (;;) {
		struct proc *c;
		bool have_child = false;

		list_for_each_entry(c, &all_procs, node) {
			if (c->ppid != me->pid)
				continue;
			if (pid > 0 && c->pid != pid)
				continue;
			have_child = true;
			if (c->state != PROC_ZOMBIE)
				continue;

			ret = c->pid;
			if (status)
				*status = c->exit_status;
			list_del(&c->node);
			local_irq_restore(flags);
			task_destroy(c->task);
			kfree(c->cmdline);
			kfree(c);
			return ret;
		}
		if (!have_child) {
			ret = -ECHILD;
			break;
		}
		waitq_wait_irq(&me->child_wq, 0);
	}
	local_irq_restore(flags);
	return ret;
}

int proc_snapshot(struct proc_info *out, int max)
{
	unsigned long flags;
	struct proc *p;
	int n = 0;

	spin_lock_irqsave(&proc_lock, flags);
	list_for_each_entry(p, &all_procs, node) {
		if (n >= max)
			break;
		out[n].pid = p->pid;
		out[n].ppid = p->ppid;
		strlcpy(out[n].comm, p->comm, sizeof(out[n].comm));
		out[n].state = p->state;
		n++;
	}
	spin_unlock_irqrestore(&proc_lock, flags);
	return n;
}
