// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tests for the desktop and its programs. The window manager runs for real
 * against a scripted backend (events in, screens out) and a pretend kernel
 * behind libq. The rules of the games are played directly.
 *
 * Set DESK_SHOTS=dir to also save every snapshot as a raw 80x25 cell dump
 * that scripts/screenshot.py turns into a PNG.
 */
#include "hosttest.h"
#include <time.h>
#include <string.h>
#include <hix/kernel.h>
#include <hix/version.h>
#include <qubix/libq.h>
#include <desk/desk.h>
#include <desk/games.h>
#include <steel/pixdev.h>

/* ---- a pretend kernel behind libq ------------------------------------ */

#define NFILES	16

static struct {
	bool used;
	char path[64];
	char data[8192];
	int len;
} files[NFILES];
static struct {
	bool used;
	int file, pos;
} fds[16];
static const char *dirs[] = { "/", "/dev", "/etc", "/tmp", "/home" };
static long fake_time = 1793609070;	/* 2026-11-02 08:44:30 UTC */

static int find_file(const char *path)
{
	int i;

	for (i = 0; i < NFILES; i++)
		if (files[i].used && !strcmp(files[i].path, path))
			return i;
	return -1;
}

static void put_file(const char *path, const char *text)
{
	int i = find_file(path);

	if (i < 0)
		for (i = 0; i < NFILES && files[i].used; i++)
			;
	files[i].used = true;
	strcpy(files[i].path, path);
	files[i].len = (int)strlen(text);
	memcpy(files[i].data, text, (size_t)files[i].len);
}

static bool dir_exists(const char *path)
{
	size_t i;

	for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
		if (!strcmp(dirs[i], path))
			return true;
	return false;
}

static void dirname_of(const char *path, char *out)
{
	const char *slash = strrchr(path, '/');

	if (slash == path) {
		strcpy(out, "/");
	} else {
		memcpy(out, path, (size_t)(slash - path));
		out[slash - path] = '\0';
	}
}

static long fake_readdir(const char *path, struct dirent_rec *recs, int max)
{
	int n = 0, i;
	size_t k;

	if (!dir_exists(path))
		return -ENOENT;
	for (k = 0; k < sizeof(dirs) / sizeof(dirs[0]) && n < max; k++) {
		char parent[64];

		if (!strcmp(dirs[k], "/"))
			continue;
		dirname_of(dirs[k], parent);
		if (strcmp(parent, path))
			continue;
		recs[n].size = 0;
		recs[n].type = VT_DIR;
		strcpy(recs[n].name, strrchr(dirs[k], '/') + 1);
		n++;
	}
	for (i = 0; i < NFILES && n < max; i++) {
		char parent[64];

		if (!files[i].used)
			continue;
		dirname_of(files[i].path, parent);
		if (strcmp(parent, path))
			continue;
		recs[n].size = (u32)files[i].len;
		recs[n].type = VT_REG;
		strcpy(recs[n].name, strrchr(files[i].path, '/') + 1);
		n++;
	}
	return n;
}

static long fake_sysctl(const char *name, char *buf, size_t len)
{
	const char *v = NULL;

	if (!strcmp(name, "kern.version"))
		v = "hix " HIX_RELEASE " (hi is not unix): aether + qubix + steel";
	else if (!strcmp(name, "kern.uptime"))
		v = "3725";
	else if (!strcmp(name, "kern.hostname") || !strcmp(name, "kern.ostype"))
		v = "hix";
	else if (!strcmp(name, "kern.osrelease"))
		v = HIX_RELEASE;
	else if (!strcmp(name, "hw.display"))
		v = "1024x768, bochs vbe";
	else if (!strcmp(name, "hix.platform"))
		v = "gantry, 12 nodes";
	else if (!strcmp(name, "hix.desktop"))
		v = "hix Desktop (steel, Argent)";
	else if (!strcmp(name, "hw.ncpu"))
		v = "1";
	else if (!strcmp(name, "hw.pagesize"))
		v = "4096";
	else if (!strcmp(name, "hw.memsize"))
		v = "134217728";
	else if (!strcmp(name, "hw.freemem"))
		v = "100663296";
	else if (!strcmp(name, "hix.heap"))
		v = "heap 1024 KiB, used 96 KiB in 41 blocks";
	else if (!strcmp(name, "hix.ps"))
		v = "PID  NAME       STATE\n  1  init    sleeping\n"
		    "  2  sh         running\n  3  desk       running\n";
	if (!v)
		return -ENOENT;
	strncpy(buf, v, len - 1);
	buf[len - 1] = '\0';
	return (long)strlen(buf);
}

long q_syscall(long nr, long a1, long a2, long a3, long a4, long a5)
{
	(void)a4;
	(void)a5;
	switch (nr) {
	case SYS_gettimeofday: {
		struct timeval_hix *tv = (struct timeval_hix *)a1;

		tv->tv_sec = fake_time;
		tv->tv_usec = 0;
		return 0;
	}
	case SYS_kill:
		return a1 == 99 ? -ESRCH : a1 <= 1 ? -EPERM : 0;
	case SYS_sysctlbyname:
		return fake_sysctl((const char *)a1, (char *)a2, (size_t)a3);
	case SYS_getdirentries:
		return fake_readdir((const char *)a1, (struct dirent_rec *)a2, (int)a3);
	case SYS_open: {
		const char *path = (const char *)a1;
		char parent[64];
		int f = find_file(path), fd;

		if (f < 0) {
			if (!(a2 & O_CREAT))
				return -ENOENT;
			dirname_of(path, parent);
			if (!dir_exists(parent))
				return -ENOENT;
			put_file(path, "");
			f = find_file(path);
		}
		if (a2 & O_TRUNC)
			files[f].len = 0;
		for (fd = 0; fd < 16 && fds[fd].used; fd++)
			;
		fds[fd].used = true;
		fds[fd].file = f;
		fds[fd].pos = 0;
		return fd + 3;
	}
	case SYS_close:
		fds[a1 - 3].used = false;
		return 0;
	case SYS_read: {
		int f = fds[a1 - 3].file, left = files[f].len - fds[a1 - 3].pos;
		int n = left < (int)a3 ? left : (int)a3;

		memcpy((void *)a2, files[f].data + fds[a1 - 3].pos, (size_t)n);
		fds[a1 - 3].pos += n;
		return n;
	}
	case SYS_write: {
		int f = fds[a1 - 3].file;

		memcpy(files[f].data + files[f].len, (void *)a2, (size_t)a3);
		files[f].len += (int)a3;
		return a3;
	}
	case SYS_mkdir: {
		size_t i;

		for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
			if (!strcmp(dirs[i], (const char *)a1))
				return -EEXIST;
		return -EPERM;	/* the pretend kernel keeps a fixed tree */
	}
	case SYS_unlink: {
		int f = find_file((const char *)a1);

		if (f < 0)
			return -ENOENT;
		files[f].used = false;
		return 0;
	}
	default:
		return -ENOSYS;
	}
}

/* ---- the scripted backend -------------------------------------------- */

enum { OP_KEY, OP_CLICK, OP_RCLICK, OP_DBL, OP_MOVE, OP_WAIT, OP_SNAP, OP_CALL,
	OP_PCLICK, OP_PDRAG, OP_PHOLD };

struct op {
	int type, a, b, mods;
	const char *name;
	void (*fn)(void);
};

#define KEY(k)		{ OP_KEY, (k), 0, 0, NULL, NULL }
#define ALT(k)		{ OP_KEY, (k), 0, MOD_ALT, NULL, NULL }
#define CLICK(x, y)	{ OP_CLICK, (x), (y), 0, NULL, NULL }
#define RCLICK(x, y)	{ OP_RCLICK, (x), (y), 0, NULL, NULL }
#define DBL(x, y)	{ OP_DBL, (x), (y), 0, NULL, NULL }
#define WAIT(ms)	{ OP_WAIT, (ms), 0, 0, NULL, NULL }
#define SNAP(n)		{ OP_SNAP, 0, 0, 0, (n), NULL }
#define PCLICK(x, y)	{ OP_PCLICK, (x), (y), 0, NULL, NULL }
/* press at (x, y), move, release at the target packed as x1 | y1 << 16 */
#define PHOLD(x, y, x1, y1) { OP_PHOLD, (x), (y), (x1) | ((y1) << 16), NULL, NULL }
#define PDRAG(x, y, x1, y1) { OP_PDRAG, (x), (y), (x1) | ((y1) << 16), NULL, NULL }
#define CALL(f)		{ OP_CALL, 0, 0, 0, NULL, (f) }

static const struct op *script;
static int nscript, spos, wait_left;
static struct input_event pending[8];
static int npending, ppos;
static unsigned long vnow = 1000;
static steel_cell_t screen[DESK_W * DESK_H];
static int presents;
static const char *shot_dir;
static int shot_count;

static void queue_mouse_px(int px, int py, int buttons)
{
	struct input_event *e = &pending[npending++];

	memset(e, 0, sizeof(*e));
	e->type = INPUT_MOUSE;
	e->px = (s16)px;
	e->py = (s16)py;
	e->x = (s16)((px - 32) / 12);		/* the grid is centred: see pixdev_init */
	e->y = (s16)((py - 24) / 28);
	e->buttons = (u8)buttons;
}

static void queue_mouse(int x, int y, int buttons)
{
	struct input_event *e = &pending[npending++];

	memset(e, 0, sizeof(*e));
	e->type = INPUT_MOUSE;
	e->x = (s16)x;
	e->y = (s16)y;
	e->px = (s16)(32 + x * 12 + 6);		/* the middle of the cell */
	e->py = (s16)(24 + y * 28 + 14);
	e->buttons = (u8)buttons;
}

/* the pixel display the tests drive: steel's own pixdev on a plain array */
static u32 tmem[1024 * 768];
static u32 tback[1024 * 768], tsprite[PIX_SPRITE_PX];
static struct pixdev tdev;
static bool tpix;			/* pixel mode on */

static void tpix_start(void)
{
	struct gfx_canvas fb;

	gfx_canvas_init(&fb, 1024, 768, tmem);
	pixdev_init(&tdev, &fb, tback, tsprite);
	desk_set_pixels(true, tdev.off_x, tdev.off_y);
	tpix = true;
}

static void tpix_stop(void)
{
	desk_set_pixels(false, 0, 0);
	tpix = false;
}

static int t_client_set(int idx, int x, int y, int w, int h, const u32 *px,
			int stride)
{
	return pixdev_client_set(&tdev, idx, x, y, w, h, px, stride);
}

static void t_client_dirty(int idx, int x, int y, int w, int h)
{
	pixdev_client_dirty(&tdev, idx, x, y, w, h);
}

static void t_client_clear(int idx)
{
	pixdev_client_clear(&tdev, idx);
}

/* what the cells look like on the pixel display: 1024x768 binary PPM */
static void save_ppm(const char *name)
{
	static u32 mem[1024 * 768];
	static struct pixdev dev;
	struct gfx_canvas fb;
	char path[256];
	FILE *f;
	int i;

	if (tpix) {
		memcpy(mem, tmem, sizeof(mem));
	} else {
		gfx_canvas_init(&fb, 1024, 768, mem);
		pixdev_init(&dev, &fb, tback, tsprite);
		pixdev_present(&dev, screen);
	}
	snprintf(path, sizeof(path), "%s/%02d-%s.ppm", shot_dir, shot_count - 1, name);
	f = fopen(path, "wb");
	if (!f)
		return;
	fprintf(f, "P6\n1024 768\n255\n");
	for (i = 0; i < 1024 * 768; i++) {
		unsigned char rgb[3] = { (unsigned char)GFX_R(mem[i]),
					 (unsigned char)GFX_G(mem[i]),
					 (unsigned char)GFX_B(mem[i]) };

		fwrite(rgb, 3, 1, f);
	}
	fclose(f);
}

static void save_shot(const char *name)
{
	char path[256];
	FILE *f;

	if (!shot_dir)
		return;
	snprintf(path, sizeof(path), "%s/%02d-%s.scr", shot_dir, shot_count++, name);
	f = fopen(path, "wb");
	if (f) {
		fwrite(screen, sizeof(screen), 1, f);
		fclose(f);
	}
	save_ppm(name);
}

static int be_wait(struct input_event *ev, long timeout)
{
	if (ppos < npending) {
		*ev = pending[ppos++];
		return 0;
	}
	npending = ppos = 0;
	if (timeout == 0)
		return -ETIMEDOUT;		/* one event per repaint */

	while (spos < nscript) {
		const struct op *o = &script[spos];

		switch (o->type) {
		case OP_WAIT: {
			long step;

			if (!wait_left)
				wait_left = o->a;
			step = timeout < 0 || timeout > wait_left ? wait_left : timeout;
			vnow += (unsigned long)step;
			wait_left -= (int)step;
			if (!wait_left)
				spos++;
			return -ETIMEDOUT;
		}
		case OP_SNAP:
			save_shot(o->name);
			spos++;
			continue;
		case OP_CALL:
			o->fn();
			spos++;
			return -ETIMEDOUT;
		case OP_KEY:
			memset(ev, 0, sizeof(*ev));
			ev->type = INPUT_KEY;
			ev->key = (u16)o->a;
			ev->mods = (u8)o->mods;
			spos++;
			return 0;
		case OP_PCLICK:
			queue_mouse_px(o->a, o->b, MOUSE_LEFT);
			queue_mouse_px(o->a, o->b, 0);
			spos++;
			*ev = pending[ppos++];
			return 0;
		case OP_PHOLD: {		/* press and drag, button stays down */
			int x1 = o->mods & 0xFFFF, y1 = (o->mods >> 16) & 0xFFFF;

			queue_mouse_px(o->a, o->b, MOUSE_LEFT);
			queue_mouse_px((o->a + x1) / 2, (o->b + y1) / 2, MOUSE_LEFT);
			queue_mouse_px(x1, y1, MOUSE_LEFT);
			spos++;
			*ev = pending[ppos++];
			return 0;
		}
		case OP_PDRAG: {
			int x1 = o->mods & 0xFFFF, y1 = (o->mods >> 16) & 0xFFFF;

			queue_mouse_px(o->a, o->b, MOUSE_LEFT);
			queue_mouse_px((o->a + x1) / 2, (o->b + y1) / 2, MOUSE_LEFT);
			queue_mouse_px(x1, y1, MOUSE_LEFT);
			queue_mouse_px(x1, y1, 0);
			spos++;
			*ev = pending[ppos++];
			return 0;
		}
		case OP_CLICK:
		case OP_RCLICK:
		case OP_DBL: {
			int btn = o->type == OP_RCLICK ? MOUSE_RIGHT : MOUSE_LEFT;

			queue_mouse(o->a, o->b, 0);
			queue_mouse(o->a, o->b, btn);
			queue_mouse(o->a, o->b, 0);
			if (o->type == OP_DBL) {
				queue_mouse(o->a, o->b, btn);
			}
			spos++;
			*ev = pending[ppos++];
			return 0;
		}
		default:
			spos++;
			break;
		}
	}
	desk_quit();
	return -ETIMEDOUT;
}

static void be_present(const steel_cell_t *cells, int w, int h)
{
	(void)w;
	(void)h;
	memcpy(screen, cells, sizeof(screen));
	if (tpix)
		pixdev_present(&tdev, cells);
	presents++;
}

static unsigned long be_now(void)
{
	return vnow;
}

static void be_panel(const struct steel_panel *p)
{
	if (tpix)
		pixdev_panel(&tdev, p);
}

static void be_float(const steel_cell_t *cells, int cx, int cy, int cw, int ch,
		     int px, int py)
{
	if (!tpix)
		return;
	if (cells)
		pixdev_float(&tdev, cells, cx, cy, cw, ch, px, py);
	else
		pixdev_float_off(&tdev);
}

static long be_wall_time(void)
{
	return fake_time;
}

static int power_calls, power_halt;

static void be_power(bool halt)
{
	power_calls++;
	power_halt = halt;
}

static const struct desk_backend backend = {
	.panel = be_panel,
	.float_win = be_float,
	.wall_time = be_wall_time,
	.power = be_power,
	.wait_event = be_wait,
	.present = be_present,
	.now_ms = be_now,
	.client_set = t_client_set,
	.client_dirty = t_client_dirty,
	.client_clear = t_client_clear,
};

static void run_script(const struct op *ops, int n, const char *start)
{
	script = ops;
	nscript = n;
	spos = 0;
	wait_left = 0;
	npending = ppos = 0;
	desk_seed(12345);
	desk_run(&backend, start);
}

#define RUN(ops, start) run_script((ops), (int)(sizeof(ops) / sizeof((ops)[0])), (start))

/* ---- looking at the screen ------------------------------------------- */

static void row_text(int y, char *out)
{
	int x;

	for (x = 0; x < DESK_W; x++) {
		u8 ch = STEEL_CH(screen[y * DESK_W + x]);

		out[x] = ch >= 0x20 && ch < 0x7F ? (char)ch : '.';
	}
	out[DESK_W] = '\0';
}

static bool find_text(const char *s, int *px, int *py)
{
	char row[DESK_W + 1];
	int y;

	for (y = 0; y < DESK_H; y++) {
		char *at;

		row_text(y, row);
		at = strstr(row, s);
		if (at) {
			if (px)
				*px = (int)(at - row);
			if (py)
				*py = y;
			return true;
		}
	}
	return false;
}

static bool on_screen(const char *s)
{
	return find_text(s, NULL, NULL);
}

static void dump_screen(void)
{
	char row[DESK_W + 1];
	int y;

	for (y = 0; y < DESK_H; y++) {
		row_text(y, row);
		printf("    |%s|\n", row);
	}
}

/* ---- desktop scenarios ----------------------------------------------- */

static void test_boot(void)
{
	static const struct op ops[] = { SNAP("desktop") };

	printf("-- desktop starts with Hub\n");
	RUN(ops, NULL);
	CHECK(presents > 0);
	CHECK(on_screen("Hub"));
	CHECK(on_screen("Main"));
	CHECK(on_screen("Games"));
	CHECK(on_screen("Files"));
	CHECK(on_screen("Minefield"));
	CHECK(on_screen("Bricks"));
	CHECK(on_screen("File"));
	CHECK(on_screen("Help"));
}

static void test_launch_by_mouse(void)
{
	int x, y;
	static const struct op ops[] = { SNAP("hub") };

	printf("-- double click on an icon starts the program\n");
	RUN(ops, NULL);
	CHECK(find_text("Calculator", &x, &y));
	{
		struct op click[] = {
			DBL(x + 3, y - 1), SNAP("calc-open"),
		};

		RUN(click, NULL);
	}
	CHECK(on_screen("Calculator"));
	CHECK(on_screen("CE"));
	CHECK(on_screen("+/-"));
}

/* what the calculator shows: its display row without the frame */
static void calc_display(char *out)
{
	char row[DESK_W + 1];
	int x, y, a, b;

	out[0] = '\0';
	if (!find_text("Calculator", &x, &y))
		return;
	row_text(y + 2, row);
	a = 0;
	b = DESK_W - 1;
	while (a < b && (row[a] == ' ' || row[a] == '.'))
		a++;
	while (b > a && (row[b] == ' ' || row[b] == '.'))
		b--;
	memcpy(out, row + a, (size_t)(b - a + 1));
	out[b - a + 1] = '\0';
}

static void calc_check(const struct op *ops, int n, const char *want)
{
	char shown[DESK_W + 1];

	run_script(ops, n, "calc");
	calc_display(shown);
	if (strcmp(shown, want))
		printf("    (display shows \"%s\")\n", shown);
	CHECK_STR(shown, want);
}

#define CALC(want, ...) do {						\
	static const struct op o_[] = { __VA_ARGS__ };			\
	calc_check(o_, (int)(sizeof(o_) / sizeof(o_[0])), (want));	\
} while (0)

static void test_calculator(void)
{
	printf("-- calculator arithmetic\n");
	CALC("42", KEY('1'), KEY('2'), KEY('+'), KEY('3'), KEY('0'), KEY('='));
	CALC("3.5", KEY('7'), KEY('/'), KEY('2'), KEY('='));
	CALC("5", KEY('1'), KEY('.'), KEY('2'), KEY('5'), KEY('*'), KEY('4'),
	     KEY('='));
	CALC("E", KEY('5'), KEY('/'), KEY('0'), KEY('='));
	CALC("-3", KEY('9'), KEY('-'), KEY('1'), KEY('2'), KEY('='));
	CALC("12", KEY('1'), KEY('2'), KEY('3'), KEY(8));
	CALC("0.1", KEY('1'), KEY('/'), KEY('1'), KEY('0'), KEY('='));
	CALC("0.333", KEY('1'), KEY('/'), KEY('3'), KEY('='));
	CALC("1.5", KEY('1'), KEY('.'), KEY('5'), KEY('+'), KEY('0'), KEY('='));
	/* classic style: no precedence, each operator runs at once */
	CALC("30", KEY('2'), KEY('+'), KEY('3'), KEY('*'), KEY('6'), KEY('='));
	CALC("-7", KEY('7'), KEY('n'));
	CALC("0", KEY('9'), KEY('c'));
	CALC("1000000", KEY('1'), KEY('0'), KEY('0'), KEY('0'), KEY('*'), KEY('1'),
	     KEY('0'), KEY('0'), KEY('0'), KEY('='));
	/* 2,147,483.647 is the largest value; one more digit is refused */
	CALC("2147483.647", KEY('2'), KEY('1'), KEY('4'), KEY('7'), KEY('4'),
	     KEY('8'), KEY('3'), KEY('.'), KEY('6'), KEY('4'), KEY('7'));
	CALC("E", KEY('2'), KEY('0'), KEY('0'), KEY('0'), KEY('0'), KEY('0'),
	     KEY('0'), KEY('*'), KEY('2'), KEY('0'), KEY('0'), KEY('0'), KEY('='));
}

/* every program in the table starts by name, paints its title, and survives
 * a few keys and a few seconds of ticking */
static void test_every_app(void)
{
	static const struct { const char *name, *title; } apps[] = {
		{ "files", "Files" }, { "notes", "Notes" },
		{ "calc", "Calculator" }, { "clock", "Clock" },
		{ "sysinfo", "System Info" }, { "tasks", "Task List" },
		{ "term", "Term" }, { "mines", "Minefield" },
		{ "reversi", "Reversi" }, { "snake", "Snake" },
		{ "bricks", "Bricks" },
	};
	size_t i;

	printf("-- every program starts and runs\n");
	for (i = 0; i < ARRAY_SIZE(apps); i++) {
		struct op ops[] = {
			WAIT(300), KEY('a'), KEY(KEY_RIGHT), KEY(KEY_DOWN),
			KEY(' '), WAIT(3000), SNAP(apps[i].name),
		};

		presents = 0;
		memset(screen, 0, sizeof(screen));
		RUN(ops, apps[i].name);
		CHECK(on_screen(apps[i].title));
		CHECK(presents > 3);
	}
}

/* ---- chess, on the pixel display ------------------------------------- */

static int ox, oy;			/* where the chess canvas sits on screen */

static int sqx(int file)
{
	return ox + 32 + file * 64 + 32;
}

static int sqy(int rank)
{
	return oy + 24 + (7 - rank) * 64 + 32;
}

static u32 pixel_at(int x, int y)
{
	return tmem[y * 1024 + x];
}

static void test_clock_sweep(void)
{
	long t;
	int n = 0;

	printf("-- clock paints at any time of day\n");
	for (t = 0; t < 2000000000L; t += 86413L * 11 + 1357, n++) {
		struct op ops[] = { WAIT(600), KEY('a') };

		fake_time = t < 2147483647L ? t : 2147483647L;
		RUN(ops, "clock");
	}
	CHECK(n > 100);
	fake_time = 1793609070;
}

static void test_chess_ui(void)
{
	struct op open[] = { WAIT(100) };

	printf("-- chess on the pixel display\n");
	tpix_start();
	RUN(open, "chess");
	CHECK(tdev.client[0].used);
	ox = tdev.client[0].x;
	oy = tdev.client[0].y;
	CHECK(tdev.client[0].w == 816 && tdev.client[0].h == 560);
	/* the starting position: a light h1... e2 is a light square with a pawn */
	CHECK(pixel_at(sqx(0), sqy(0) + 28) != 0);

	tpix_start();
	{
		struct op ops[] = {
			WAIT(100), SNAP("chess-start"),
			PCLICK(sqx(4), sqy(1)), WAIT(60), SNAP("chess-selected"),
			PCLICK(sqx(4), sqy(3)), WAIT(60), SNAP("chess-moved"),
			WAIT(3000), SNAP("chess-reply"),
		};

		RUN(ops, "chess");
	}
	/* e2 is empty again: a plain light square, its pawn is on e4 */
	CHECK(pixel_at(sqx(4) - 28, sqy(1) - 28) == 0xEEEED2);
	CHECK(pixel_at(sqx(4), sqy(3) + 10) != 0xEEEED2 &&
	      pixel_at(sqx(4), sqy(3) + 10) != 0x769656);
	/* the computer answered: Black has a piece on the move highlight somewhere */
	{
		int hl = 0, f, r;

		for (r = 4; r < 8; r++)
			for (f = 0; f < 8; f++) {
				u32 c = pixel_at(sqx(f) - 28, sqy(r) - 28);

				if (c == 0xF5F682 || c == 0xBACA2B)
					hl++;
			}
		CHECK(hl >= 1);
	}

	/* selecting a piece shows where it can go */
	tpix_start();
	{
		struct op ops[] = {
			WAIT(100), PCLICK(sqx(6), sqy(0)), WAIT(60), SNAP("chess-knight"),
		};

		RUN(ops, "chess");
	}
	CHECK(pixel_at(sqx(5), sqy(2)) != 0x769656 &&
	      pixel_at(sqx(5), sqy(2)) != 0xEEEED2);	/* f3 dot */
	CHECK(pixel_at(sqx(7), sqy(2)) != 0x769656 &&
	      pixel_at(sqx(7), sqy(2)) != 0xEEEED2);	/* h3 dot */
	CHECK(pixel_at(sqx(4), sqy(3)) == 0x769656 ||
	      pixel_at(sqx(4), sqy(3)) == 0xEEEED2);	/* e4: no dot */

	/* dragging works too */
	tpix_start();
	{
		struct op ops[] = {
			WAIT(100), PDRAG(sqx(3), sqy(1), sqx(3), sqy(3)), WAIT(60),
			SNAP("chess-dragged"),
		};

		RUN(ops, "chess");
	}
	CHECK(pixel_at(sqx(3) - 28, sqy(1) - 28) == 0x769656 ||
	      pixel_at(sqx(3) - 28, sqy(1) - 28) == 0xEEEED2);	/* d2 left, no highlight yet */
	CHECK(pixel_at(sqx(3), sqy(3) + 10) != 0x769656 &&
	      pixel_at(sqx(3), sqy(3) + 10) != 0xEEEED2 &&
	      pixel_at(sqx(3), sqy(3) + 10) != 0xBACA2B &&
	      pixel_at(sqx(3), sqy(3) + 10) != 0xF5F682);	/* a pawn stands on d4 */

	/* a click on an empty square does nothing; clicking the other side's piece neither */
	tpix_start();
	{
		struct op ops[] = {
			WAIT(100), PCLICK(sqx(4), sqy(4)), PCLICK(sqx(4), sqy(6)),
			WAIT(60), SNAP("chess-idle"),
		};

		RUN(ops, "chess");
	}
	CHECK(pixel_at(sqx(4) - 28, sqy(6) - 28) == 0x769656 ||
	      pixel_at(sqx(4) - 28, sqy(6) - 28) == 0xEEEED2);

	/* without pixels the program says so instead of drawing garbage */
	tpix_stop();
	{
		struct op ops[] = { WAIT(100) };

		RUN(ops, "chess");
	}
	CHECK(on_screen("pixels"));
}

/* where button @i of the dock is, with two windows open (Hub and one more) */
static int task_px(int i)
{
	struct steel_panel p;
	int x = 0, w = 0;

	memset(&p, 0, sizeof(p));
	p.nmenus = 2;
	p.menu[0].x = 36;
	p.menu[0].w = (s16)panel_menu_width("Apps");
	p.menu[1].x = (s16)(36 + p.menu[0].w);
	p.menu[1].w = (s16)panel_menu_width("Computer");
	strcpy(p.clock, "08:44");
	p.ntasks = 2;
	panel_task_rect(1024, &p, i, &x, &w);
	return x;
}

/* turn a line of text into key presses, ending with Enter */
static int type_line(struct op *o, int n, const char *text)
{
	for (; *text; text++)
		o[n++] = (struct op){ OP_KEY, (unsigned char)*text, 0, 0, NULL, NULL };
	o[n++] = (struct op){ OP_KEY, '\n', 0, 0, NULL, NULL };
	return n;
}

static void test_term_commands(void)
{
	static struct op ops[128];
	int n = 0;

	printf("-- Term: hixfetch and kill\n");
	ops[n++] = (struct op){ OP_WAIT, 50, 0, 0, NULL, NULL };
	n = type_line(ops, n, "hixfetch");
	ops[n++] = (struct op){ OP_SNAP, 0, 0, 0, "term-hixfetch", NULL };
	n = type_line(ops, n, "cls");
	n = type_line(ops, n, "kill 99");
	n = type_line(ops, n, "kill 1");
	n = type_line(ops, n, "kill 7");
	n = type_line(ops, n, "kill x");
	ops[n++] = (struct op){ OP_SNAP, 0, 0, 0, "term-kill", NULL };
	tpix_start();
	run_script(ops, n, "term");
	CHECK(on_screen("No such process"));
	CHECK(on_screen("may not be killed"));
	CHECK(on_screen("Signalled"));
	CHECK(on_screen("usage: kill"));

	n = 0;
	n = type_line(ops, n, "hixfetch");
	/* the Hub goes to the taskbar: raise it, then minimise it */
	ops[n++] = (struct op){ OP_PCLICK, task_px(0) + 10, 12, 0, NULL, NULL };
	ops[n++] = (struct op){ OP_WAIT, 50, 0, 0, NULL, NULL };
	ops[n++] = (struct op){ OP_PCLICK, task_px(0) + 10, 12, 0, NULL, NULL };
	ops[n++] = (struct op){ OP_WAIT, 50, 0, 0, NULL, NULL };
	ops[n++] = (struct op){ OP_SNAP, 0, 0, 0, "desktop-hixfetch", NULL };
	run_script(ops, n, "term");
	CHECK(on_screen("hi is not unix"));
	CHECK(on_screen("OS: hix " HIX_RELEASE));
	CHECK(on_screen("Kernel: aether + qubix + steel"));
	CHECK(on_screen("Platform: gantry, 12 nodes"));
	CHECK(on_screen("Uptime: 62m 05s"));
	CHECK(on_screen("Memory: 32 MiB / 128 MiB"));
	CHECK(on_screen("DE: hix Desktop"));
	tpix_stop();
}

static void test_smooth_drag(void)
{
	static const struct op open_files[] = {
		WAIT(50), KEY(KEY_F(9)), KEY('\n'), WAIT(100),
	};
	int cx, cy, px, py, x0, y0, i;
	bool same = true;

	printf("-- smooth drag\n");
	tpix_start();
	RUN(open_files, NULL);
	CHECK(find_text("Files", &cx, &cy));
	px = 32 + cx * 12 + 6;
	py = 24 + cy * 28 + 14;
	x0 = 32 + cx * 12;		/* not exact: the title is inside the window */
	(void)x0;
	{
		struct op ops[] = {
			WAIT(50), KEY(KEY_F(9)), KEY('\n'), WAIT(100),
			PHOLD(px, py, px + 5, py + 3), SNAP("drag-float"),
		};

		tpix_start();
		RUN(ops, NULL);
	}
	CHECK(tdev.fl_on);
	CHECK((tdev.fl_x - tdev.off_x) % PIX_CELL_W == 5);
	CHECK((tdev.fl_y - tdev.off_y) % PIX_CELL_H == 3);
	/* what is on the screen is the sprite, byte for byte */
	for (i = 0; i < 200; i++) {
		int sx = (i * 37) % tdev.fl_w, sy = (i * 53) % tdev.fl_h;

		if (tmem[(tdev.fl_y + sy) * 1024 + tdev.fl_x + sx] !=
		    tdev.sprite[sy * tdev.fl_w + sx])
			same = false;
	}
	CHECK(same);
	y0 = tdev.fl_y;
	(void)y0;
	/* and when the button is released, it snaps to the grid */
	{
		struct op ops[] = {
			WAIT(50), KEY(KEY_F(9)), KEY('\n'), WAIT(100),
			PDRAG(px, py, px + 30, py + 40), WAIT(50),
		};

		tpix_start();
		RUN(ops, NULL);
	}
	CHECK(!tdev.fl_on);
	CHECK(find_text("Files", &i, &y0) && i == cx + 3 && y0 == cy + 1);

	/* a window may be pulled out at the left side of the screen */
	{
		struct op ops[] = {
			WAIT(50), KEY(KEY_F(9)), KEY('\n'), WAIT(100),
			PHOLD(px, py, 6, py), SNAP("drag-off-left"),
		};

		tpix_start();
		RUN(ops, NULL);
		CHECK(tdev.fl_on && tdev.fl_x < 0);
		CHECK(tdev.fl_x + tdev.fl_w > 4 * PIX_CELL_W);	/* but not all of it */
	}
	{
		struct op ops[] = {
			WAIT(50), KEY(KEY_F(9)), KEY('\n'), WAIT(100),
			PDRAG(px, py, 6, py), WAIT(50), SNAP("off-left-dropped"),
		};

		tpix_start();
		RUN(ops, NULL);
		CHECK(!tdev.fl_on && tdev.panel.ntasks == 2);
		/* the right part of the window is still there: its frame is drawn */
		CHECK(GFX_R(tmem[300 * 1024 + 40]) > 150);
	}

	/* what one frame of the drag costs: only a rough guide, the host is fast */
	{
		static steel_cell_t one[PIX_COLS * PIX_ROWS];
		struct timespec t0, t1;
		int n, fr = 300;

		for (n = 0; n < PIX_COLS * PIX_ROWS; n++)
			one[n] = STEEL_CELL('x', 0x1F);
		tpix_start();
		pixdev_float(&tdev, one, 10, 3, 60, 16, 100, 100);
		clock_gettime(CLOCK_MONOTONIC, &t0);
		for (n = 0; n < fr; n++)
			pixdev_float(&tdev, one, 10, 3, 60, 16, 100 + n % 200, 100 + n % 90);
		clock_gettime(CLOCK_MONOTONIC, &t1);
		printf("  a 720x448 window dragged, as a sprite: %ld us per frame\n",
		       ((t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec)) /
		       1000 / fr);

		/* the same window moved by whole cells: every cell it covers is redrawn */
		{
			static steel_cell_t grid[PIX_COLS * PIX_ROWS];
			int x, y;

			tpix_start();
			clock_gettime(CLOCK_MONOTONIC, &t0);
			for (n = 0; n < fr; n++) {
				for (x = 0; x < PIX_COLS * PIX_ROWS; x++)
					grid[x] = STEEL_CELL(' ', 0x07);
				for (y = 3; y < 19; y++)
					for (x = 10 + n % 8; x < 70 + n % 8; x++)
						grid[y * PIX_COLS + x] =
							STEEL_CELL('a' + (x - n % 8 + y) % 26,
								   0x1F);
				pixdev_present(&tdev, grid);
			}
			clock_gettime(CLOCK_MONOTONIC, &t1);
			printf("  the same, redrawn cell by cell:        %ld us per frame\n",
			       ((t1.tv_sec - t0.tv_sec) * 1000000000L +
				(t1.tv_nsec - t0.tv_nsec)) / 1000 / fr);
		}
	}
	tpix_stop();
}

static void test_panels(void)
{
	int tx;
	static const struct op idle[] = { WAIT(100), SNAP("panel-idle") };
	static const struct op menu[] = {
		WAIT(50), PCLICK(50, 12), WAIT(50), SNAP("panel-apps"),
	};
	static const struct op launch[] = {
		WAIT(50), KEY(KEY_F(9)), WAIT(50), KEY('\n'), WAIT(100),
		SNAP("panel-two-windows"),
	};

	printf("-- the panels\n");
	tpix_start();
	RUN(idle, NULL);
	CHECK(tdev.have_panel && tdev.panel.nmenus == 2);
	CHECK(!strcmp(tdev.panel.menu[0].label, "Apps"));
	CHECK(!strcmp(tdev.panel.menu[1].label, "Computer"));
	CHECK(!strcmp(tdev.panel.clock, "08:44"));
	CHECK(tdev.panel.ntasks == 1 && (tdev.panel.task[0].flags & PT_ACTIVE));
	CHECK(GFX_R(tmem[12 * 1024 + 600]) > 190);		/* the top bar is light */
	CHECK(GFX_R(tmem[(768 - 4) * 1024 + 600]) < 100);	/* no bar below: the desktop */

	tpix_start();
	RUN(menu, NULL);
	CHECK(tdev.panel.open == 0);
	CHECK(on_screen("Calculator") && on_screen("Term"));
	CHECK(!on_screen("Restart"));

	tpix_start();			/* F9, Enter: the first program starts */
	RUN(launch, NULL);
	CHECK(tdev.panel.ntasks == 2 && tdev.panel.open == -1);
	CHECK((tdev.panel.task[1].flags & PT_ACTIVE) &&
	      !(tdev.panel.task[0].flags & PT_ACTIVE));
	CHECK(!strcmp(tdev.panel.task[1].title, "Files"));

	/* a click on a task button minimises, another restores */
	tx = task_px(1) + 10;
	{
		struct op ops[] = {
			WAIT(50), KEY(KEY_F(9)), KEY('\n'), WAIT(50),
			PCLICK(tx, 12), WAIT(50), SNAP("panel-min"),
		};

		tpix_start();
		RUN(ops, NULL);
		CHECK(tdev.panel.ntasks == 2 && (tdev.panel.task[1].flags & PT_MIN));
	}
	{
		struct op ops[] = {
			WAIT(50), KEY(KEY_F(9)), KEY('\n'), WAIT(50),
			PCLICK(tx, 12), WAIT(50),
			PCLICK(tx, 12), WAIT(50), SNAP("panel-restored"),
		};

		tpix_start();
		RUN(ops, NULL);
		CHECK(!(tdev.panel.task[1].flags & PT_MIN) &&
		      (tdev.panel.task[1].flags & PT_ACTIVE));
	}
	{
		/* Computer > Restart asks first, then calls the backend */
		struct op ops[] = {
			WAIT(50), KEY(KEY_F(9)), KEY(KEY_RIGHT),
			KEY(KEY_DOWN), KEY(KEY_DOWN), KEY(KEY_DOWN), KEY(KEY_DOWN),
			KEY(KEY_DOWN), WAIT(50), SNAP("panel-computer"),
			KEY('\n'), WAIT(50), SNAP("panel-restart-ask"),
		};

		tpix_start();
		power_calls = 0;
		RUN(ops, NULL);
		CHECK(power_calls == 0 && on_screen("Restart the computer?"));
	}
	{
		struct op ops[] = {
			WAIT(50), KEY(KEY_F(9)), KEY(KEY_RIGHT),
			KEY(KEY_DOWN), KEY(KEY_DOWN), KEY(KEY_DOWN), KEY(KEY_DOWN),
			KEY(KEY_DOWN), KEY(KEY_DOWN), KEY('\n'), WAIT(50),
			KEY('\n'), WAIT(50),
		};

		tpix_start();
		power_calls = 0;
		RUN(ops, NULL);
		CHECK(power_calls == 1 && power_halt);
	}
	{
		/* clicks in the top margin never reach a window underneath */
		struct op ops[] = { WAIT(50), PCLICK(500, 3), WAIT(50) };

		tpix_start();
		RUN(ops, NULL);
		CHECK(tdev.panel.ntasks == 1 && !(tdev.panel.task[0].flags & PT_MIN));
	}
	tpix_stop();
}

int main(void)
{
	printf("test_desk\n");
	shot_dir = getenv("DESK_SHOTS");
	tpix_start();			/* the real desktop runs on the pixel display */
	test_every_app();
	tpix_stop();			/* the hand-drawing code is the cell one */
	test_clock_sweep();
	tpix_start();
	test_chess_ui();
	tpix_start();
	test_boot();
	test_launch_by_mouse();
	test_calculator();
	tpix_stop();
	test_panels();
	test_smooth_drag();
	test_term_commands();
	(void)dump_screen;
	TEST_DONE();
}
