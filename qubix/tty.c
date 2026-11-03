// SPDX-License-Identifier: GPL-2.0-only
/*
 * qubix: the console tty.
 *
 * Input drivers push characters into a ring from interrupt context. Readers
 * sleep on a wait queue; the line discipline (echo, backspace, Ctrl-U/C/D/L)
 * runs in the reader's own context, so interrupts stay short.
 */
#include <hix/kernel.h>
#include <hix/spinlock.h>
#include <aether/waitq.h>
#include <steel/steel.h>
#include <qubix/tty.h>

#define RING_SIZE	128		/* power of two */

static int ring[RING_SIZE];
static unsigned int ring_head, ring_tail;
static DECLARE_WAITQ(input_wq);
static bool raw_mode;
static unsigned long raw_timeout_ms;

void tty_input(int c)
{
	unsigned int next = (ring_head + 1) & (RING_SIZE - 1);

	if (next == ring_tail)
		return;			/* full: drop */
	ring[ring_head] = c;
	ring_head = next;
	waitq_wake_one(&input_wq);
}

void tty_set_raw(bool on)
{
	raw_mode = on;
	if (!on)
		raw_timeout_ms = 0;
}

bool tty_is_raw(void)
{
	return raw_mode;
}

void tty_set_timeout(unsigned long ms)
{
	raw_timeout_ms = ms;
}

/* one key; -ETIMEDOUT when @ticks pass first (0 = wait forever) */
static int tty_getc_timeout(unsigned long ticks)
{
	unsigned long flags;
	int c;

	local_irq_save(flags);
	while (ring_head == ring_tail) {
		if (waitq_wait_irq(&input_wq, ticks) && ring_head == ring_tail) {
			local_irq_restore(flags);
			return -ETIMEDOUT;
		}
	}
	c = ring[ring_tail];
	ring_tail = (ring_tail + 1) & (RING_SIZE - 1);
	local_irq_restore(flags);
	return c;
}

int tty_getc(void)
{
	return tty_getc_timeout(0);
}

ssize_t tty_write(const char *buf, size_t len)
{
	steel_con_write(buf, len);
	return (ssize_t)len;
}

ssize_t tty_read(char *buf, size_t len)
{
	size_t n = 0;

	if (!len)
		return 0;

	if (raw_mode) {
		unsigned long ticks = raw_timeout_ms ?
			(raw_timeout_ms * HZ + 999) / 1000 : 0;
		int key;

		if (len < sizeof(key))
			return -EINVAL;
		key = tty_getc_timeout(ticks);
		if (key < 0)
			return key;
		memcpy(buf, &key, sizeof(key));
		return (ssize_t)sizeof(key);
	}

	for (;;) {
		int c = tty_getc();

		if (c == '\n') {
			steel_con_write("\n", 1);
			if (n < len)
				buf[n++] = '\n';
			return (ssize_t)n;
		}
		if (c == 4) {			/* Ctrl-D */
			if (n == 0)
				return 0;
			continue;
		}
		if (c == 3) {			/* Ctrl-C: abandon the line */
			steel_con_write("^C\n", 3);
			buf[0] = '\n';
			return 1;
		}
		if (c == 12) {			/* Ctrl-L */
			steel_con_write("\x1b[2J", 4);
			continue;
		}
		if (c == 21) {			/* Ctrl-U: kill line */
			while (n) {
				steel_con_write("\b", 1);
				n--;
			}
			continue;
		}
		if (c == '\b' || c == 0x7F) {
			if (n) {
				n--;
				steel_con_write("\b", 1);
			}
			continue;
		}
		if (c < 0x20 && c != '\t')
			continue;
		if (n + 1 < len) {		/* keep room for the newline */
			char ch = (char)c;

			buf[n++] = ch;
			steel_con_write(&ch, 1);
		}
	}
}
