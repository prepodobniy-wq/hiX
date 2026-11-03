/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * qubix: system call numbers.
 *
 * One trap (int 0x80) serves two worlds: positive numbers are process
 * system calls, negative numbers are port traps. Where practical the numbers
 * follow the common Unix numbering. ABI: %eax = number, arguments in %ebx %ecx %edx
 * %esi %edi, result in %eax. process calls return -errno on failure; port traps
 * return a ae_ret_t.
 */
#ifndef _QUBIX_SYSCALL_H
#define _QUBIX_SYSCALL_H

/* process calls */
#define SYS_exit		1
#define SYS_read		3
#define SYS_write		4
#define SYS_open		5
#define SYS_close		6
#define SYS_wait4		7
#define SYS_unlink		10
#define SYS_getpid		20
#define SYS_getppid		39
#define SYS_ioctl		54	/* (fd, request, arg): tty modes */
#define SYS_reboot		55
#define SYS_gettimeofday	116
#define SYS_mkdir		136
#define SYS_getdirentries	196	/* simplified: (path, buf, max) */
#define SYS_lseek		199
#define SYS_posix_spawn		244	/* simplified: (command line) */
#define SYS_sysctlbyname	274	/* (name, buf, buflen) -> text */
#define SYS_steelctl		500	/* hix: graphics control */

/* port traps */
#define AE_reply_port		(-26)
#define AE_thread_self	(-27)
#define AE_task_self		(-28)
#define AE_msg_trap		(-31)	/* (msg, option, rcv_name, timeout) */
#define AE_swtch		(-60)	/* yield */
#define AE_clock_sleep	(-62)	/* simplified: (milliseconds) */
#define AE_bootstrap_register	(-100)	/* hix: (name, port) */
#define AE_bootstrap_look_up	(-101)	/* hix: (name, &port) */
#define AE_port_deallocate	(-102)
#define AE_port_insert_send	(-103)

/* reboot(2) flags, as in <sys/reboot.h> */
#define RB_AUTOBOOT		0
#define RB_HALT			0x08
#define RB_PANIC		0x10000	/* hix: test the panic path */

/*
 * steelctl operations: SYS_steelctl(op, a, b, c)
 *
 * A full-screen program (the desktop) GRABs the display: the console stops
 * drawing and the keyboard and mouse deliver input_events to it instead of
 * the tty. The grab ends with UNGRAB or when the process exits.
 */
#define STEEL_CTL_DEMO		0	/* the kernel's built-in steel demo */
#define STEEL_CTL_GRAB		1	/* take the display and input; -EBUSY */
#define STEEL_CTL_UNGRAB	2
#define STEEL_CTL_PRESENT	3	/* (cells, w, h): show a w*h surface */
#define STEEL_CTL_EVENT		4	/* (event *, timeout_ms; <0 = forever) */
#define STEEL_CTL_INFO		5	/* (int out[2]): width, height */
#define STEEL_CTL_MSEC		6	/* milliseconds since boot */
#define STEEL_CTL_GFXINFO	7	/* (int out[6]): w, h, cell w, cell h, x, y */
#define STEEL_CTL_CLIENT_SET	8	/* (struct steel_client_req *) */
#define STEEL_CTL_CLIENT_CLEAR	9	/* (index) */
#define STEEL_CTL_CLIENT_DIRTY	10	/* (struct steel_client_req *) */

/*
 * GRAB takes a flag: 1 asks for the pixel display. The result is 0 for the
 * text display, 1 when the pixel display is active, or a negative error.
 */
#define STEEL_GRAB_TEXT		0
#define STEEL_GRAB_PIXELS	1

/* a window that shows its own pixels, see steel/pixdev.h */
struct steel_client_req {
	int idx;			/* 0 .. PIX_CLIENTS-1 */
	int x, y, w, h;			/* SET: screen rectangle; DIRTY: client rectangle */
	unsigned long px;		/* SET: the pixels, 0x00RRGGBB */
	int stride;			/* SET: pixels per row */
};

struct timeval_hix {
	long tv_sec;
	long tv_usec;
};

struct pt_regs;
void qubix_init(void);
void qubix_syscall(struct pt_regs *regs);

#endif /* _QUBIX_SYSCALL_H */
