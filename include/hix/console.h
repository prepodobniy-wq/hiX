/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _HIX_CONSOLE_H
#define _HIX_CONSOLE_H

#include <hix/types.h>
#include <hix/list.h>

/*
 * A kernel message sink. printk() fans every line out to all registered
 * consoles (serial port, steel text console, ...). Consoles must be callable
 * from any context, including interrupt handlers and panic().
 */
struct console {
	const char *name;
	int max_level;		/* print messages with level <= max_level */
	void (*write)(struct console *con, int level, const char *s, size_t n);
	struct list_head node;
};

/* registering replays the kernel log so early messages are not lost */
void register_console(struct console *con);

#endif /* _HIX_CONSOLE_H */
