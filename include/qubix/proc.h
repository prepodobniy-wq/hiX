/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * qubix: processes. A struct proc is the process face of an aether task:
 * pid, parent, exit status, file descriptors. Its program runs in the
 * task's first thread.
 */
#ifndef _QUBIX_PROC_H
#define _QUBIX_PROC_H

#include <hix/types.h>
#include <hix/list.h>
#include <aether/task.h>
#include <aether/waitq.h>
#include <qubix/vfs.h>

#define NOFILE		16
#define PROC_ARGS_MAX	8

enum proc_state { PROC_RUNNING, PROC_ZOMBIE };

struct proc {
	pid_t pid, ppid;
	char comm[TASK_NAME_LEN];
	enum proc_state state;
	int exit_status;
	struct task *task;
	struct file *fds[NOFILE];
	struct list_head node;
	struct waitq child_wq;		/* parents sleep here in wait4() */
	char *cmdline;			/* backing store for argv */
};

/* a built-in program, the equivalent of an executable on disk */
struct qprog {
	const char *name;
	const char *help;
	int (*main)(int argc, char **argv);
};

void proc_init(void);
struct proc *proc_current(void);
pid_t proc_spawn(const char *cmdline);			/* pid or -errno */
long proc_wait(pid_t pid, int *status);			/* reaped pid or -errno */
void proc_exit(int status) __attribute__((noreturn));
void qubix_display_release(struct proc *p);		/* syscall.c */

int proc_fd_alloc(struct proc *p, struct file *f);	/* fd or -EMFILE */
struct file *proc_fd_get(struct proc *p, int fd);
int proc_fd_close(struct proc *p, int fd);

struct proc_info {
	pid_t pid, ppid;
	char comm[TASK_NAME_LEN];
	enum proc_state state;
};
int proc_snapshot(struct proc_info *out, int max);

extern const struct qprog qprogs[];
extern const int qprogs_count;

#endif /* _QUBIX_PROC_H */
