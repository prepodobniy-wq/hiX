/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * libq: the "libc" of qubix programs. Everything here goes through the
 * int 0x80 trap, never through direct kernel calls, so a program written
 * against libq keeps working when it moves to user mode.
 */
#ifndef _QUBIX_LIBQ_H
#define _QUBIX_LIBQ_H

#include <hix/types.h>
#include <hix/printk.h>
#include <aether/ipc.h>
#include <qubix/syscall.h>
#include <qubix/vfs.h>

#ifdef HIX_HOST_TEST
/* host tests supply their own kernel: see tests/test_desk.c */
long q_syscall(long nr, long a1, long a2, long a3, long a4, long a5);
#else
static inline long q_syscall(long nr, long a1, long a2, long a3, long a4,
			     long a5)
{
	long ret;

	__asm__ volatile("int $0x80"
			 : "=a"(ret)
			 : "a"(nr), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
			 : "memory", "cc");
	return ret;
}
#endif

#define Q0(nr)			q_syscall(nr, 0, 0, 0, 0, 0)
#define Q1(nr, a)		q_syscall(nr, (long)(a), 0, 0, 0, 0)
#define Q2(nr, a, b)		q_syscall(nr, (long)(a), (long)(b), 0, 0, 0)
#define Q3(nr, a, b, c)		q_syscall(nr, (long)(a), (long)(b), (long)(c), 0, 0)
#define Q4(nr, a, b, c, d)	q_syscall(nr, (long)(a), (long)(b), (long)(c), \
					  (long)(d), 0)

/* ---- process side -------------------------------------------------------- */

static inline void q_exit(int status)
{
	Q1(SYS_exit, status);
	for (;;)
		;
}

static inline long q_read(int fd, void *buf, size_t n)
{
	return Q3(SYS_read, fd, buf, n);
}

static inline long q_write(int fd, const void *buf, size_t n)
{
	return Q3(SYS_write, fd, buf, n);
}

static inline long q_open(const char *path, int flags)
{
	return Q2(SYS_open, path, flags);
}

static inline long q_close(int fd)
{
	return Q1(SYS_close, fd);
}

static inline long q_lseek(int fd, long off, int whence)
{
	return Q3(SYS_lseek, fd, off, whence);
}

static inline long q_unlink(const char *path)
{
	return Q1(SYS_unlink, path);
}

static inline long q_mkdir(const char *path)
{
	return Q1(SYS_mkdir, path);
}

static inline long q_getpid(void)
{
	return Q0(SYS_getpid);
}

static inline long q_getppid(void)
{
	return Q0(SYS_getppid);
}

static inline long q_spawn(const char *cmdline)
{
	return Q1(SYS_posix_spawn, cmdline);
}

static inline long q_wait4(long pid, int *status)
{
	return Q3(SYS_wait4, pid, status, 0);
}

static inline long q_readdir(const char *path, struct dirent_rec *recs,
			     int max)
{
	return Q3(SYS_getdirentries, path, recs, max);
}

static inline long q_sysctl(const char *name, char *buf, size_t len)
{
	return Q3(SYS_sysctlbyname, name, buf, len);
}

static inline long q_ioctl(int fd, long req, long arg)
{
	return q_syscall(SYS_ioctl, fd, req, arg, 0, 0);
}

static inline long q_reboot(int howto)
{
	return Q1(SYS_reboot, howto);
}

static inline long q_time(void)
{
	struct timeval_hix tv;

	if (Q2(SYS_gettimeofday, &tv, 0) < 0)
		return -1;
	return tv.tv_sec;
}

static inline long q_steelctl(int op, long a, long b, long c)
{
	return Q4(SYS_steelctl, op, a, b, c);
}

static inline void q_puts(const char *s)
{
	size_t n = 0;

	while (s[n])
		n++;
	q_write(1, s, n);
}

/* printf to stdout (formats via the kernel's vsnprintf; 256 bytes max) */
static inline int q_printf(const char *fmt, ...) __printf(1, 2);
static inline int q_printf(const char *fmt, ...)
{
	char buf[256];
	__builtin_va_list ap;
	int n;

	__builtin_va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	__builtin_va_end(ap);
	if (n >= (int)sizeof(buf))
		n = sizeof(buf) - 1;
	q_write(1, buf, (size_t)n);
	return n;
}

/* ---- port side ------------------------------------------------------- */

static inline ae_port_name_t q_ae_reply_port(void)
{
	return (ae_port_name_t)Q0(AE_reply_port);
}

static inline ae_ret_t q_ae_msg(struct ae_msg *msg, u32 option,
				       ae_port_name_t rcv_name,
				       unsigned long timeout_ms)
{
	return (ae_ret_t)Q4(AE_msg_trap, msg, option, rcv_name,
				 timeout_ms);
}

static inline ae_ret_t q_bootstrap_register(const char *name,
						 ae_port_name_t port)
{
	return (ae_ret_t)Q2(AE_bootstrap_register, name, port);
}

static inline ae_ret_t q_bootstrap_look_up(const char *name,
						ae_port_name_t *port)
{
	return (ae_ret_t)Q2(AE_bootstrap_look_up, name, port);
}

static inline ae_ret_t q_port_deallocate(ae_port_name_t port)
{
	return (ae_ret_t)Q1(AE_port_deallocate, port);
}

static inline void q_yield(void)
{
	Q0(AE_swtch);
}

static inline void q_sleep_ms(unsigned long ms)
{
	Q1(AE_clock_sleep, ms);
}

#endif /* _QUBIX_LIBQ_H */
