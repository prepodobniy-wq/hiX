// SPDX-License-Identifier: GPL-2.0-only
/*
 * printk: formatted kernel messages with log levels.
 *
 * Every message is timestamped, stored in a ring of fixed-size records (that
 * is what `dmesg` reads) and handed to all registered consoles. Because the
 * log is kept, a console registered late replays everything printed before
 * it existed.
 */
#include <stdarg.h>
#include <hix/kernel.h>
#include <hix/console.h>
#include <hix/spinlock.h>

#define LOG_RECORDS	256
#define LOG_TEXT	120		/* longer lines are cut in the log only */
#define LINE_MAX	256

struct log_record {
	u8 level;
	u8 len;
	char text[LOG_TEXT];
};

static struct log_record log_ring[LOG_RECORDS];
static unsigned long log_seq;		/* total records ever written */

static LIST_HEAD(console_list);
static DEFINE_SPINLOCK(printk_lock);
static int in_printk;

static void log_append(int level, const char *line, size_t len)
{
	struct log_record *rec = &log_ring[log_seq % LOG_RECORDS];

	if (len > LOG_TEXT)
		len = LOG_TEXT;
	rec->level = (u8)level;
	rec->len = (u8)len;
	memcpy(rec->text, line, len);
	log_seq++;
}

static void console_emit(int level, const char *line, size_t len)
{
	struct list_head *pos;

	list_for_each(pos, &console_list) {
		struct console *con = list_entry(pos, struct console, node);

		if (level <= con->max_level)
			con->write(con, level, line, len);
	}
}

void register_console(struct console *con)
{
	unsigned long flags;
	unsigned long seq, first;

	spin_lock_irqsave(&printk_lock, flags);
	list_add_tail(&con->node, &console_list);

	/* replay what was logged before we existed */
	first = log_seq > LOG_RECORDS ? log_seq - LOG_RECORDS : 0;
	for (seq = first; seq < log_seq; seq++) {
		struct log_record *rec = &log_ring[seq % LOG_RECORDS];

		if (rec->level <= con->max_level)
			con->write(con, rec->level, rec->text, rec->len);
	}
	spin_unlock_irqrestore(&printk_lock, flags);
}

/* after a panic the lock owner may never come back: take the sinks anyway */
void printk_bust_locks(void)
{
	in_printk = 0;
	spin_lock_init(&printk_lock);
}

int printk(const char *fmt, ...)
{
	char msg[LINE_MAX];
	char line[LINE_MAX + 16];
	int level = LOGLEVEL_DEFAULT;
	unsigned long flags;
	size_t len;
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	{
		const char *text = msg;

		if (text[0] == KERN_SOH[0] && text[1] >= '0' && text[1] <= '7') {
			level = text[1] - '0';
			text += 2;
		}
		len = (size_t)snprintf(line, sizeof(line), "[%5lu.%02lu] %s",
				       jiffies / HZ, jiffies % HZ, text);
	}
	if (len >= sizeof(line))
		len = sizeof(line) - 1;

	spin_lock_irqsave(&printk_lock, flags);
	log_append(level, line, len);
	if (!in_printk) {
		in_printk = 1;
		console_emit(level, line, len);
		in_printk = 0;
	}
	spin_unlock_irqrestore(&printk_lock, flags);
	return (int)len;
}

/* copy the newest part of the log that fits into @buf; returns the length */
size_t printk_log_read(char *buf, size_t size)
{
	unsigned long flags;
	unsigned long seq, first;
	size_t total = 0, pos = 0;

	if (!size)
		return 0;

	spin_lock_irqsave(&printk_lock, flags);
	first = log_seq > LOG_RECORDS ? log_seq - LOG_RECORDS : 0;

	/* skip the oldest records until the rest fits */
	for (seq = first; seq < log_seq; seq++)
		total += log_ring[seq % LOG_RECORDS].len;
	for (seq = first; seq < log_seq && total > size; seq++)
		total -= log_ring[seq % LOG_RECORDS].len;

	for (; seq < log_seq; seq++) {
		struct log_record *rec = &log_ring[seq % LOG_RECORDS];

		memcpy(buf + pos, rec->text, rec->len);
		pos += rec->len;
	}
	spin_unlock_irqrestore(&printk_lock, flags);
	return pos;
}
