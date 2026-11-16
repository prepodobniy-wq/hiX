/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * libhix: the SDK for programs that run in ring 3.
 *
 * No libc: system calls are `int 0x80`, strings and a small printf are in
 * libhix.c, and the Kit calls (Gantry services) are at the bottom.
 * Build with scripts/mkapps.py (see docs/SDK.md).
 */
#ifndef HIX_SDK_H
#define HIX_SDK_H

#include "../../include/hix/kitabi.h"
#include "../../include/hix/cryptoabi.h"

#define HX_SYS_exit	1
#define HX_SYS_read	3
#define HX_SYS_write	4
#define HX_SYS_open	5
#define HX_SYS_close	6
#define HX_SYS_wait4	7
#define HX_SYS_unlink	10
#define HX_SYS_getpid	20
#define HX_SYS_kill	37
#define HX_SYS_getppid	39
#define HX_SYS_gettimeofday	116
#define HX_SYS_mkdir	136
#define HX_SYS_getdirentries	196
#define HX_SYS_lseek	199
#define HX_SYS_spawn	244
#define HX_SYS_sysctl	274
#define HX_SYS_crypto	601
#define HX_SYS_sleep	(-62)

#define HX_O_RDONLY	0
#define HX_O_WRONLY	1
#define HX_O_RDWR	2
#define HX_O_CREAT	0100
#define HX_O_TRUNC	01000
#define HX_SEEK_SET	0
#define HX_SEEK_CUR	1
#define HX_SEEK_END	2

#define HX_NAME_MAX	24

/* one directory entry, as hx_readdir() returns it */
struct hx_dirent {
	unsigned int size;
	unsigned int type;		/* 0 file, 1 directory, 2 device */
	char name[HX_NAME_MAX];
};

typedef unsigned int hx_size;

static inline long hx_syscall(long n, long a, long b, long c)
{
	long r;

	__asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c)
			 : "memory");
	return r;
}

static inline long hx_write(int fd, const void *buf, hx_size len)
{
	return hx_syscall(HX_SYS_write, fd, (long)buf, (long)len);
}

static inline long hx_read(int fd, void *buf, hx_size len)
{
	return hx_syscall(HX_SYS_read, fd, (long)buf, (long)len);
}

static inline long hx_getpid(void)
{
	return hx_syscall(HX_SYS_getpid, 0, 0, 0);
}

static inline long hx_getppid(void)
{
	return hx_syscall(HX_SYS_getppid, 0, 0, 0);
}

static inline void hx_exit(int code)
{
	hx_syscall(HX_SYS_exit, code, 0, 0);
	for (;;)
		;
}

static inline long hx_syscall4(long n, long a, long b, long c, long d)
{
	long r;

	__asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c),
			 "S"(d) : "memory");
	return r;
}

/* files and directories: results are bytes/fds or -errno */
static inline long hx_open(const char *path, int flags)
{
	return hx_syscall(HX_SYS_open, (long)path, flags, 0);
}

static inline long hx_close(int fd)
{
	return hx_syscall(HX_SYS_close, fd, 0, 0);
}

static inline long hx_lseek(int fd, long off, int whence)
{
	return hx_syscall(HX_SYS_lseek, fd, off, whence);
}

static inline long hx_mkdir(const char *path)
{
	return hx_syscall(HX_SYS_mkdir, (long)path, 0, 0);
}

static inline long hx_unlink(const char *path)
{
	return hx_syscall(HX_SYS_unlink, (long)path, 0, 0);
}

/* up to @max entries of @path; returns the count or -errno */
static inline long hx_readdir(const char *path, struct hx_dirent *out, int max)
{
	return hx_syscall(HX_SYS_getdirentries, (long)path, (long)out, max);
}

/* processes and time */
static inline long hx_spawn(const char *cmdline)	/* pid or -errno */
{
	return hx_syscall(HX_SYS_spawn, (long)cmdline, 0, 0);
}

static inline long hx_wait(long pid, int *status)
{
	return hx_syscall(HX_SYS_wait4, pid, (long)status, 0);
}

static inline long hx_kill(long pid)
{
	return hx_syscall(HX_SYS_kill, pid, 9, 0);
}

static inline void hx_sleep_ms(unsigned int ms)
{
	hx_syscall(HX_SYS_sleep, ms, 0, 0);
}

static inline long hx_time(void)			/* seconds since 1970 */
{
	long tv[2];

	return hx_syscall(HX_SYS_gettimeofday, (long)tv, 0, 0) < 0 ? -1 : tv[0];
}

/* text sysctl, e.g. "hix.registry": returns bytes or -errno */
static inline long hx_sysctl(const char *name, char *buf, hx_size size)
{
	return hx_syscall(HX_SYS_sysctl, (long)name, (long)buf, (long)size);
}

/* strings and memory (libhix.c) */
void *memcpy(void *d, const void *s, hx_size n);
void *memset(void *d, int c, hx_size n);
hx_size hx_strlen(const char *s);
int hx_strcmp(const char *a, const char *b);
long hx_atoi(const char *s);
/* write a whole file, read a whole file (up to @size - 1 bytes, NUL ended) */
long hx_write_file(const char *path, const void *data, hx_size len);
long hx_read_file(const char *path, char *buf, hx_size size);
void hx_puts(const char *s);
/* %s %c %d %u %x %% with optional width and 0 flag */
int hx_printf(const char *fmt, ...);

/* crypto: ring 0 Rust modules behind SYS_crypto, see include/hix/cryptoabi.h.
 * The kernel copies all buffers; @req is in/out and returns req->result. */
static inline long hx_crypto(unsigned int op, struct hix_crypto_req *req)
{
	return hx_syscall(HX_SYS_crypto, (long)op, (long)req, 0);
}

/* the Kit: Gantry services, see include/hix/kitabi.h */
long kit_lookup(const char *service);
long kit_call(long handle, unsigned int op, struct kit_msg *m);
/* convenience over kit.prop: value into @out, returns length or -errno */
long kit_prop_get(const char *key, char *out, hx_size size);
long kit_prop_set(const char *key, const char *val);	/* keys under "user." */
long kit_notify_count(const char *topic);
long kit_notify_post(const char *topic);		/* topics under "user." */

#endif /* HIX_SDK_H */
