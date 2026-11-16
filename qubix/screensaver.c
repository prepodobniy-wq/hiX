// SPDX-License-Identifier: GPL-2.0-only
/*
 * qubix: screensavers. Four of them, all plain terminal programs:
 *
 *   stars   a slowly falling starfield
 *   matrix  the falling green glyph rain
 *   clock   a large UTC clock with the date
 *   blank   just a dark screen
 *
 * Any key leaves the screensaver. The shell starts one after the configured
 * idle time; `screensaver [name]` runs one by hand.
 */
#include <hix/kernel.h>
#include <hix/bootconfig.h>
#include <qubix/libq.h>
#include <qubix/splash.h>
#include <qubix/syscall.h>
#include <qubix/tty.h>

#define SV_ROWS		25
#define SV_COLS		79

#define C_RESET		"\x1b[0m"
#define C_WHITE		"\x1b[1;37m"
#define C_TEXT		"\x1b[0;37m"
#define C_DIM		"\x1b[0;90m"
#define C_LCYAN		"\x1b[1;36m"
#define C_GREEN		"\x1b[1;32m"
#define C_DGREEN	"\x1b[0;32m"

static u32 rng_state;

static u32 rnd(void)
{
	rng_state = rng_state * 1664525u + 1013904223u;
	return rng_state;
}

static void at(int row, int col)
{
	q_printf("\x1b[%d;%dH", row, col);
}

static void line_at(int row, const char *text, const char *color)
{
	char line[SV_COLS];
	size_t len = strlen(text);
	size_t left;

	if (len > sizeof(line))
		len = sizeof(line);
	left = (sizeof(line) - len) / 2;
	memset(line, ' ', sizeof(line));
	memcpy(line + left, text, len);
	at(row, 1);
	q_puts(color);
	q_write(1, line, sizeof(line));
	q_puts(C_RESET);
}

static bool key_now(void)
{
	int key;

	return q_read(0, &key, sizeof(key)) == (long)sizeof(key);
}

/* ---- stars ----------------------------------------------------------- */

struct star {
	int x, y, speed;
	u8 kind;			/* 0 = '.', 1 = '+', 2 = '*' */
};

static struct star stars[40];

static char star_char(u8 kind)
{
	return kind == 2 ? '*' : kind == 1 ? '+' : '.';
}

static const char *star_color(u8 kind)
{
	if (kind == 2)
		return C_WHITE;
	if (kind == 1)
		return C_LCYAN;
	return C_DIM;
}

static void star_place(struct star *s)
{
	s->x = 1 + (int)(rnd() % SV_COLS);
	s->y = 1 + (int)(rnd() % SV_ROWS);
	s->speed = 1 + (int)(rnd() % 2);
	s->kind = (u8)(rnd() % 3);
}

static void run_stars(void)
{
	int i;

	q_puts("\x1b[2J\x1b[H");
	for (i = 0; i < 30; i++) {
		at(1 + (int)(rnd() % SV_ROWS), 1 + (int)(rnd() % SV_COLS));
		q_puts(C_DIM ".");
	}
	for (i = 0; i < (int)ARRAY_SIZE(stars); i++) {
		star_place(&stars[i]);
		at(stars[i].y, stars[i].x);
		q_printf("%s%c" C_RESET, star_color(stars[i].kind),
			 star_char(stars[i].kind));
	}

	q_ioctl(0, TTY_SETTIMEOUT, 20);
	while (!key_now()) {
		for (i = 0; i < (int)ARRAY_SIZE(stars); i++) {
			struct star *s = &stars[i];

			at(s->y, s->x);
			q_puts(" ");
			s->y += s->speed;
			if (s->y > SV_ROWS) {
				star_place(s);
				s->y = 1;
			}
			if (rnd() % 32 == 0)
				s->kind = (u8)(rnd() % 3);
			at(s->y, s->x);
			q_printf("%s%c" C_RESET, star_color(s->kind),
				 star_char(s->kind));
		}
		q_sleep_ms(70);
	}
}

/* ---- matrix ---------------------------------------------------------- */

static u8 mat_power[SV_ROWS][SV_COLS];
static u8 mat_glyph[SV_ROWS][SV_COLS];
static int mat_head[SV_COLS];
static bool mat_active[SV_COLS];

static const char matrix_chars[] =
	"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ@#$%&*+=<>";

static void mat_paint(int y, int x)
{
	u8 p = mat_power[y][x];

	at(y + 1, x + 1);
	if (!p) {
		q_puts(" ");
		return;
	}
	if (p == 3)
		q_puts(C_WHITE);
	else if (p == 2)
		q_puts(C_GREEN);
	else
		q_puts(C_DGREEN);
	q_printf("%c" C_RESET, mat_glyph[y][x]);
}

static void run_matrix(void)
{
	int x, y;

	q_puts("\x1b[2J\x1b[H");
	memset(mat_power, 0, sizeof(mat_power));
	for (x = 0; x < SV_COLS; x++) {
		mat_active[x] = rnd() % 3 == 0;
		mat_head[x] = -(int)(rnd() % 30);
	}

	q_ioctl(0, TTY_SETTIMEOUT, 20);
	while (!key_now()) {
		for (y = 0; y < SV_ROWS; y++) {
			for (x = 0; x < SV_COLS; x++) {
				if (!mat_power[y][x])
					continue;
				mat_power[y][x]--;
				mat_paint(y, x);
			}
		}
		for (x = 0; x < SV_COLS; x++) {
			if (!mat_active[x])
				continue;
			mat_head[x]++;
			if (mat_head[x] < 0)
				continue;
			if (mat_head[x] >= SV_ROWS) {
				mat_head[x] = -(int)(rnd() % 24);
				continue;
			}
			y = mat_head[x];
			mat_glyph[y][x] = (u8)matrix_chars[
				rnd() % (sizeof(matrix_chars) - 1)];
			mat_power[y][x] = 3;
			mat_paint(y, x);
		}
		q_sleep_ms(90);
	}
}

/* ---- clock ----------------------------------------------------------- */

static void clock_face(long now)
{
	char text[96];
	char up[32] = "0";
	int y, mo, d, h, mi, s, wd;
	unsigned long secs;

	if (now < 0) {
		line_at(12, "clock unavailable", C_DIM);
		return;
	}
	hix_time_parts(now, &y, &mo, &d, &h, &mi, &s, &wd);
	hix_big_clock(9, h, mi, s);
	snprintf(text, sizeof(text), "%s, %d %s %d",
		 hix_wday_name(wd), d, hix_month_name(mo), y);
	line_at(16, text, C_TEXT);
	line_at(18, "UTC", C_DIM);
	q_sysctl("kern.uptime", up, sizeof(up));
	secs = simple_strtoul(up, NULL, 10);
	snprintf(text, sizeof(text), "up %lu:%02lu:%02lu", secs / 3600,
		 secs % 3600 / 60, secs % 60);
	line_at(20, text, C_DIM);
}

static void run_clock(void)
{
	long last = -2;

	q_puts("\x1b[2J\x1b[H");
	line_at(24, "press any key", C_DIM);
	q_ioctl(0, TTY_SETTIMEOUT, 100);
	for (;;) {
		long now = q_time();

		if (now != last) {
			last = now;
			clock_face(now);
		}
		if (key_now())
			break;
	}
}

/* ---- blank ----------------------------------------------------------- */

static void run_blank(void)
{
	q_puts("\x1b[2J\x1b[H");
	q_ioctl(0, TTY_SETTIMEOUT, 200);
	while (!key_now())
		;
}

/* ---- entry ----------------------------------------------------------- */

int screensaver_main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : hix_boot.screensaver;
	int err = 0;

	rng_state = (u32)q_steelctl(STEEL_CTL_MSEC, 0, 0, 0) ^
		    ((u32)q_getpid() << 16);
	rng_state ^= (u32)q_time();
	q_ioctl(0, TTY_SETTIMEOUT, 0);
	q_ioctl(0, TTY_SETRAW, 1);

	if (!strcmp(name, "stars"))
		run_stars();
	else if (!strcmp(name, "matrix"))
		run_matrix();
	else if (!strcmp(name, "clock"))
		run_clock();
	else if (!strcmp(name, "blank"))
		run_blank();
	else {
		q_ioctl(0, TTY_SETRAW, 0);
		q_printf("screensaver: unknown '%s' "
			 "(stars, matrix, clock, blank)\n", name);
		return 1;
	}

	q_ioctl(0, TTY_SETTIMEOUT, 0);
	q_ioctl(0, TTY_SETRAW, 0);
	q_puts("\x1b[2J\x1b[H");
	return err;
}
