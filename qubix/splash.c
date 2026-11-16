// SPDX-License-Identifier: GPL-2.0-only
/*
 * qubix: the boot splash.
 *
 * A full-screen start page in the spirit of a fancy terminal setup: the hix
 * logo, a live UTC clock, a short status line and a small progress bar. It is
 * a normal qubix process, so it draws only through fd 1 and can be skipped by
 * pressing any key.
 */
#include <hix/kernel.h>
#include <hix/bootconfig.h>
#include <hix/version.h>
#include <qubix/libq.h>
#include <qubix/splash.h>
#include <qubix/syscall.h>
#include <qubix/tty.h>

#define SP_ROWS		25
#define SP_COLS		80
#define IN_LEFT		3
#define IN_RIGHT	78
#define IN_WIDTH	(IN_RIGHT - IN_LEFT + 1)

#define C_RESET		"\x1b[0m"
#define C_BORDER	"\x1b[0;36m"
#define C_LOGO		"\x1b[1;36m"
#define C_WHITE		"\x1b[1;37m"
#define C_TEXT		"\x1b[0;37m"
#define C_DIM		"\x1b[0;90m"

#define LOGO_COL	5
#define INFO_COL	50

/*
 * A wide block "hix" in five terminal rows. The source is a 38x10 bitmap
 * folded two rows at a time into the CP437 half-block glyphs (0xDB/0xDC/0xDF),
 * so the diagonals stay smooth in VGA text mode and in the pixel console.
 */
static const char * const logo[] = {
	"\xDB\xDB\xDB\xDB            \xDB\xDB\xDB\xDB    \xDF\xDB\xDB\xDB\xDC"
	"    \xDC\xDB\xDB\xDB\xDF",
	"\xDB\xDB\xDB\xDB            \xDC\xDC\xDC\xDC      \xDF\xDB\xDB\xDB"
	"\xDC\xDC\xDB\xDB\xDB\xDF  ",
	"\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB    \xDB\xDB\xDB\xDB"
	"        \xDF\xDB\xDB\xDB\xDB\xDF    ",
	"\xDB\xDB\xDB\xDB    \xDB\xDB\xDB\xDB    \xDB\xDB\xDB\xDB       \xDC"
	"\xDB\xDB\xDB\xDB\xDB\xDB\xDC   ",
	"\xDB\xDB\xDB\xDB    \xDB\xDB\xDB\xDB    \xDB\xDB\xDB\xDB     \xDC"
	"\xDB\xDB\xDB\xDF  \xDF\xDB\xDB\xDB\xDC ",
};

/* 4x5 block digits; the clock is 39 columns wide */
static const char * const digit[10][5] = {
	{ " ## ", "#  #", "#  #", "#  #", " ## " },
	{ "  # ", " ## ", "  # ", "  # ", "####" },
	{ "### ", "   #", " ## ", "#   ", "####" },
	{ "### ", "   #", " ## ", "   #", "### " },
	{ "#  #", "#  #", "####", "   #", "   #" },
	{ "####", "#   ", "### ", "   #", "### " },
	{ " ## ", "#   ", "### ", "#  #", " ## " },
	{ "####", "   #", "  # ", " #  ", " #  " },
	{ " ## ", "#  #", " ## ", "#  #", " ## " },
	{ " ## ", "#  #", " ###", "   #", " ## " },
};

static const char * const colon[5] = {
	"    ", " ## ", " ## ", " ## ", "    ",
};

static const char * const month_name[12] = {
	"January", "February", "March", "April", "May", "June",
	"July", "August", "September", "October", "November", "December",
};

static const char * const wday_name[7] = {
	"Sunday", "Monday", "Tuesday", "Wednesday",
	"Thursday", "Friday", "Saturday",
};

void hix_time_parts(long epoch, int *year, int *month, int *day,
		    int *hour, int *minute, int *second, int *wday)
{
	long days = epoch / 86400;
	long secs = epoch % 86400;
	long z, era, doe, yoe, doy, mp;
	long y, m, d;

	if (secs < 0) {
		secs += 86400;
		days--;
	}
	z = days + 719468;
	era = (z >= 0 ? z : z - 146096) / 146097;
	doe = z - era * 146097;
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	y = yoe + era * 400;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	d = doy - (153 * mp + 2) / 5 + 1;
	m = mp < 10 ? mp + 3 : mp - 9;
	y += m <= 2;

	*year = (int)y;
	*month = (int)m;
	*day = (int)d;
	*hour = (int)(secs / 3600);
	*minute = (int)(secs % 3600 / 60);
	*second = (int)(secs % 60);
	*wday = (int)((days + 4) % 7);		/* 1970-01-01 was a Thursday */
	if (*wday < 0)
		*wday += 7;
}

const char *hix_month_name(int month)
{
	if (month < 1 || month > 12)
		return "?";
	return month_name[month - 1];
}

const char *hix_wday_name(int wday)
{
	if (wday < 0 || wday > 6)
		return "?";
	return wday_name[wday];
}

static void at(int row, int col)
{
	q_printf("\x1b[%d;%dH", row, col);
}

/* a fixed-width line inside the frame; old text cannot leave a tail behind */
static void inner_line(int row, const char *text, const char *color)
{
	char line[IN_WIDTH];
	size_t len = strlen(text);
	size_t left;

	if (len > sizeof(line))
		len = sizeof(line);
	left = (sizeof(line) - len) / 2;
	memset(line, ' ', sizeof(line));
	memcpy(line + left, text, len);
	at(row, IN_LEFT);
	q_puts(color);
	q_write(1, line, sizeof(line));
	q_puts(C_RESET);
}

void hix_big_clock(int row, int hour, int minute, int second)
{
	int values[8] = {
		hour / 10, hour % 10, -1,
		minute / 10, minute % 10, -1,
		second / 10, second % 10,
	};
	char line[64];
	int r, i;
	size_t pos;

	for (r = 0; r < 5; r++) {
		pos = 0;
		for (i = 0; i < 8; i++) {
			const char *pat = values[i] < 0 ? colon[r] :
					  digit[values[i]][r];

			memcpy(line + pos, pat, 4);
			pos += 4;
			if (i < 7)
				line[pos++] = ' ';
		}
		line[pos] = '\0';
		at(row + r, (SP_COLS - (int)pos) / 2 + 1);
		q_printf(C_WHITE "%s" C_RESET, line);
	}
}

static void frame(void)
{
	char line[IN_WIDTH + 2];
	char sides[1024];
	size_t n = 0;
	int row, i;

	line[0] = '+';
	for (i = 1; i < IN_WIDTH + 1; i++)
		line[i] = '-';
	line[IN_WIDTH + 1] = '+';

	q_puts(C_BORDER);
	at(2, 2);
	q_write(1, line, sizeof(line));
	at(24, 2);
	q_write(1, line, sizeof(line));
	for (row = 3; row <= 23; row++)
		n += (size_t)snprintf(sides + n, sizeof(sides) - n,
				      "\x1b[%d;2H|\x1b[%d;79H|", row, row);
	q_write(1, sides, n);
	q_puts(C_RESET);
}

static void draw_logo(void)
{
	int i;

	for (i = 0; i < (int)ARRAY_SIZE(logo); i++) {
		at(4 + i, LOGO_COL);
		q_printf(C_LOGO "%s" C_RESET, logo[i]);
	}
	at(4, INFO_COL);
	q_printf(C_WHITE "%s %s" C_RESET, HIX_NAME, HIX_RELEASE);
	at(5, INFO_COL);
	q_puts(C_TEXT HIX_EXPAND C_RESET);
	at(6, INFO_COL);
	q_puts(C_DIM "aether + qubix + steel" C_RESET);
	at(7, INFO_COL);
	q_puts(C_DIM "console  80x25 text" C_RESET);
	at(8, INFO_COL);
	{
		char host[32] = "hix";

		q_sysctl("kern.hostname", host, sizeof(host));
		q_printf(C_DIM "host     %s" C_RESET, host);
	}
}

static void draw_time(long now)
{
	char text[80];
	int y, mo, d, h, mi, s, wd;

	if (now < 0) {
		inner_line(12, "clock unavailable", C_DIM);
		return;
	}
	hix_time_parts(now, &y, &mo, &d, &h, &mi, &s, &wd);
	hix_big_clock(10, h, mi, s);
	snprintf(text, sizeof(text), "%s, %d %s %d",
		 hix_wday_name(wd), d, hix_month_name(mo), y);
	inner_line(16, text, C_TEXT);
}

static void draw_status(void)
{
	char mem[32] = "0";
	char crypto[80] = "Rust crypto";
	char status[160];
	unsigned long mib;

	q_sysctl("hw.memsize", mem, sizeof(mem));
	mib = simple_strtoul(mem, NULL, 10) / (1024 * 1024);
	q_sysctl("hix.crypto", crypto, sizeof(crypto));
	if (strstr(crypto, "unavailable"))
		strlcpy(crypto, "no Rust crypto", sizeof(crypto));
	else if (strstr(crypto, "failed"))
		strlcpy(crypto, "crypto self-test failed", sizeof(crypto));
	else
		strlcpy(crypto, "ChaCha20-Poly1305 ready", sizeof(crypto));
	snprintf(status, sizeof(status), "%lu MiB RAM  |  %s", mib, crypto);
	inner_line(17, status, C_DIM);
}

static void draw_progress(unsigned long elapsed, unsigned long total,
			  const char *label)
{
	char out[320];
	int pct = total ? (int)(elapsed * 100 / total) : 100;
	int fill, i;
	size_t n = 0;

	if (pct > 100)
		pct = 100;
	fill = pct * 40 / 100;
	n += (size_t)snprintf(out + n, sizeof(out) - n,
			      "\x1b[19;17H\x1b[1;32m[");
	for (i = 0; i < fill; i++)
		out[n++] = '#';
	n += (size_t)snprintf(out + n, sizeof(out) - n, "\x1b[0;90m");
	for (; i < 40; i++)
		out[n++] = '-';
	n += (size_t)snprintf(out + n, sizeof(out) - n,
			      "\x1b[0m] %3d%%\x1b[20;3H\x1b[0;37m%-76s\x1b[0m",
			      pct, label);
	q_write(1, out, n);
}

static const char *stage_label(int pct)
{
	if (pct < 20)
		return "memory and page tables";
	if (pct < 40)
		return "aether: scheduler and ports";
	if (pct < 60)
		return "gantry: devices and drivers";
	if (pct < 80)
		return "qubix: processes and files";
	if (pct < 100)
		return "starting the shell";
	return "ready";
}

static unsigned long splash_msec(void)
{
	return (unsigned long)q_steelctl(STEEL_CTL_MSEC, 0, 0, 0);
}

int splash_main(int argc, char **argv)
{
	unsigned long total, start;
	long last = -2;
	int timeout = hix_boot.splash_seconds;

	if (argc > 1)
		timeout = (int)simple_strtoul(argv[1], NULL, 10);
	if (timeout <= 0)
		return 0;

	total = (unsigned long)timeout * 1000;
	q_ioctl(0, TTY_SETTIMEOUT, 0);
	q_ioctl(0, TTY_SETRAW, 1);
	q_puts("\x1b[2J");
	frame();
	draw_logo();
	draw_status();
	at(22, 3);
	inner_line(22, "press any key to skip", C_DIM);

	start = splash_msec();
	for (;;) {
		unsigned long elapsed = splash_msec() - start;
		long now;
		int key = 0, pct;

		if (elapsed >= total)
			break;
		now = q_time();
		if (now != last) {
			last = now;
			draw_time(now);
		}
		pct = (int)(elapsed * 100 / total);
		draw_progress(elapsed, total, stage_label(pct));
		q_ioctl(0, TTY_SETTIMEOUT, 100);
		if (q_read(0, &key, sizeof(key)) == (long)sizeof(key))
			break;
	}

	q_ioctl(0, TTY_SETTIMEOUT, 0);
	q_ioctl(0, TTY_SETRAW, 0);
	q_puts("\x1b[2J\x1b[H");
	return 0;
}
