/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _HIX_SERIAL_H
#define _HIX_SERIAL_H

#include <hix/types.h>

/*
 * COM1 is both a printk console and a mirror of the qubix tty, so
 * `make run-serial` gives a complete shell over one wire. No-op when the
 * UART was not found.
 */
void serial_tty_write(const char *s, size_t n);

#endif /* _HIX_SERIAL_H */
