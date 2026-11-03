/* SPDX-License-Identifier: GPL-2.0-only */
/* qubix: the console terminal with a cooked line discipline. */
#ifndef _QUBIX_TTY_H
#define _QUBIX_TTY_H

#include <hix/types.h>
#include <qubix/input.h>

/* feed one input character; safe to call from interrupt context */
void tty_input(int c);

/* line-buffered read: blocks until Enter, returns bytes (0 on Ctrl-D) */
ssize_t tty_read(char *buf, size_t len);
ssize_t tty_write(const char *buf, size_t len);

/* raw read of one key, blocks; used by full-screen programs */
int tty_getc(void);

/*
 * Raw mode: no echo, no line editing. read() on the console returns one key
 * per call as an int (ASCII or a KEY_* code), so a program such as the shell
 * can do its own editing. A timeout (ms, 0 = none) makes read() give up with
 * -ETIMEDOUT. Raw mode is cleared when the process that set it exits.
 */
#define TTY_SETRAW	0x7401		/* ioctl arg: 1 = raw, 0 = cooked */
#define TTY_SETTIMEOUT	0x7402		/* ioctl arg: milliseconds */
void tty_set_raw(bool on);
bool tty_is_raw(void);
void tty_set_timeout(unsigned long ms);

#endif /* _QUBIX_TTY_H */
