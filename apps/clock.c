// SPDX-License-Identifier: GPL-2.0-only
/*
 * desk: Clock. An analog face drawn with integer trigonometry, or digits.
 * The time comes from the real time clock through gettimeofday().
 */
#include <hix/kernel.h>
#include <qubix/libq.h>
#include <desk/desk.h>

/* sin(2*pi*k/60) * 1000 */
static const s16 sin60[60] = {
	0, 105, 208, 309, 407, 500, 588, 669, 743, 809, 866, 914, 951, 978,
	995, 1000, 995, 978, 951, 914, 866, 809, 743, 669, 588, 500, 407,
	309, 208, 105, 0, -105, -208, -309, -407, -500, -588, -669, -743,
	-809, -866, -914, -951, -978, -995, -1000, -995, -978, -951, -914,
	-866, -809, -743, -669, -588, -500, -407, -309, -208, -105,
};

enum { CK_ANALOG = 1, CK_DIGITAL };

struct clk {
	bool analog;
	long drawn;			/* the second the pixel face shows */
	bool dirty;
};

/* the pixel face, when the desktop runs on the pixel display */
#define CK_PX_W		360
#define CK_PX_H		420
static u32 pixels[CK_PX_W * CK_PX_H];
static u32 backdrop[CK_PX_W * CK_PX_H];	/* the face without hands, drawn once */
static struct gfx_canvas canvas, back;
static bool back_valid;
static char backdate[24];

static const struct menu_item set_items[] = {
	{ "Analog", CK_ANALOG, NULL },
	{ "Digital", CK_DIGITAL, NULL },
	{ "", 0, NULL },
	{ "Exit", CMD_CLOSE, "Alt+F4" },
};
static const struct menu menus[] = {
	{ "Settings", set_items, ARRAY_SIZE(set_items) },
	DESK_HELP_MENU,
};

/* days since 1970-01-01 to a calendar date (proleptic Gregorian) */
static void civil(long days, int *y, int *m, int *d)
{
	long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
	long doe = z - era * 146097;
	long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	long mp = (5 * doy + 2) / 153;

	*d = (int)(doy - (153 * mp + 2) / 5 + 1);
	*m = (int)(mp < 10 ? mp + 3 : mp - 9);
	*y = (int)(yoe + era * 400 + (*m <= 2));
}

static void line(struct steel_surface *c, int x0, int y0, int x1, int y1,
		 u8 ch, u8 attr)
{
	int dx = abs(x1 - x0), dy = -abs(y1 - y0);
	int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
	int err = dx + dy, left = dx - dy + 1;

	/* @left is the number of cells on the line: a hard stop, never a hang */
	while (left-- > 0) {
		int e2 = 2 * err;		/* one value for both tests */

		steel_put(c, x0, y0, ch, attr);
		if (x0 == x1 && y0 == y1)
			break;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

/* a hand at @k sixtieths of a turn, @len of the radius in percent */
static void hand(struct steel_surface *c, int cx, int cy, int rx, int ry,
		 int k, int len, u8 ch, u8 attr)
{
	int ex = cx + sin60[k % 60] * rx * len / 100000;
	int ey = cy - sin60[(k + 15) % 60] * ry * len / 100000;

	line(c, cx, cy, ex, ey, ch, attr);
}

static void clk_open(struct win *w, const char *arg)
{
	struct clk *k = DESK_PRIV(w, struct clk);

	(void)arg;
	k->analog = true;
	k->drawn = -1;
	if (desk_pixels()) {
		gfx_canvas_init(&canvas, CK_PX_W, CK_PX_H, pixels);
		w->pix = &canvas;
	}
}

/* a point at @k sixtieths of a turn, @r pixels from the centre, in 1/16 pixel */
static int px16(int c, int k, int r, bool y)
{
	return c * 16 + (y ? -sin60[(k + 15) % 60] : sin60[k % 60]) * r * 16 / 1000;
}

static void px_hand(int cx, int cy, int k, int len, int tail, int width, u32 color)
{
	gfx_line16(&canvas, px16(cx, k, -tail, false), px16(cy, k, -tail, true),
		   px16(cx, k, len, false), px16(cy, k, len, true), width * 16, color,
		   255);
}

/* the unchanging part of the face: bezel, ticks, numerals, the date */
static void px_backdrop(struct gfx_canvas *cv, int cx, int cy, const char *date)
{
	int i, w = CK_PX_W;

	gfx_vgrad(cv, 0, 0, CK_PX_W, CK_PX_H, 0xFAFAF8, 0xE6E8EC);
	gfx_disc16(cv, cx * 16, (cy + 3) * 16, 156 * 16, 0x000000, 40);	/* shadow */
	gfx_disc16(cv, cx * 16, cy * 16, 154 * 16, 0x6F7581, 255);	/* bezel */
	gfx_disc16(cv, cx * 16, cy * 16, 150 * 16, 0xC9CDD5, 255);
	gfx_disc16(cv, cx * 16, cy * 16, 146 * 16, 0xFCFCFA, 255);	/* face */
	for (i = 0; i < 60; i++) {
		bool hour = i % 5 == 0;
		int r0 = hour ? 118 : 130, r1 = 138;

		gfx_line16(cv, px16(cx, i, r0, false), px16(cy, i, r0, true),
			   px16(cx, i, r1, false), px16(cy, i, r1, true),
			   hour ? 56 : 20, hour ? 0x22252B : 0x8B9099, 255);
	}
	gfx_text_center(cv, &gfx_font_uib, cx - 30, cy - 98, 60, "12", 0x22252B);
	gfx_text_center(cv, &gfx_font_uib, cx - 30, cy + 80, 60, "6", 0x22252B);
	gfx_text_center(cv, &gfx_font_uib, cx + 80, cy - 11, 40, "3", 0x22252B);
	gfx_text_center(cv, &gfx_font_uib, cx - 120, cy - 11, 40, "9", 0x22252B);
	gfx_text_center(cv, &gfx_font_ui, cx - 60, cy + 40, 120, "hix", 0x8B9099);
	gfx_text_center(cv, &gfx_font_uib, 0, 352, w, date, 0x2A2D34);
}


static void px_face(struct clk *k, long t)
{
	long days = t / 86400, secs = t % 86400;
	int hh = (int)(secs / 3600), mm = (int)(secs / 60 % 60), ss = (int)(secs % 60);
	int y, mo, d, cx, cy;
	static const char *const mon[12] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug",
		"Sep", "Oct", "Nov", "Dec",
	};
	static const char *const dow[7] = {
		"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed",
	};
	char buf[40];
	int w = k->analog ? 360 : 288, h = k->analog ? 420 : 140;

	gfx_canvas_init(&canvas, w, h, pixels);
	civil(days, &y, &mo, &d);
	snprintf(buf, sizeof(buf), "%s %d %s %d", dow[days % 7], d, mon[mo - 1], y);

	if (!k->analog) {
		char tm[16];

		gfx_vgrad(&canvas, 0, 0, w, h, 0xFAFAF8, 0xE6E8EC);
		snprintf(tm, sizeof(tm), "%02d:%02d:%02d", hh, mm, ss);
		gfx_text_center(&canvas, &gfx_font_uib, 0, 36, w, tm, 0x2F5DA8);
		gfx_text_center(&canvas, &gfx_font_ui, 0, 84, w, buf, 0x4A4D55);
		return;
	}
	cx = w / 2;
	cy = 176;
	if (!back_valid || strcmp(buf, backdate)) {	/* the date changed, or first time */
		gfx_canvas_init(&back, CK_PX_W, CK_PX_H, backdrop);
		px_backdrop(&back, cx, cy, buf);
		strlcpy(backdate, buf, sizeof(backdate));
		back_valid = true;
	}
	memcpy(pixels, backdrop, sizeof(pixels));
	px_hand(cx, cy, (hh % 12) * 5 + mm / 12, 80, 10, 8, 0x22252B);
	px_hand(cx, cy, mm, 118, 14, 5, 0x22252B);
	px_hand(cx, cy, ss, 130, 26, 2, 0xD24A3C);
	gfx_disc16(&canvas, cx * 16, cy * 16, 8 * 16, 0xD24A3C, 255);
	gfx_disc16(&canvas, cx * 16, cy * 16, 3 * 16, 0xFCFCFA, 255);
}

static void clk_paint(struct win *w, struct steel_surface *c)
{
	struct clk *k = DESK_PRIV(w, struct clk);
	long t = q_time();
	long days = t / 86400, secs = t % 86400;
	int hh = (int)(secs / 3600), mm = (int)(secs / 60 % 60);
	int ss = (int)(secs % 60), y, mo, d, i;
	static const char *const mon[12] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug",
		"Sep", "Oct", "Nov", "Dec",
	};
	static const char *const dow[7] = {
		"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed",
	};
	char buf[40];

	if (t < 0) {
		steel_puts(c, 2, c->h / 2, "No real time clock", A_WIN);
		return;
	}
	if (w->pix) {			/* redraw the pixel face once per second */
		if (k->drawn != t || k->dirty) {
			k->drawn = t;
			k->dirty = false;
			px_face(k, t);
			desk_px_dirty(w, 0, 0, CK_PX_W, CK_PX_H);
		}
		return;
	}
	civil(days, &y, &mo, &d);
	snprintf(buf, sizeof(buf), "%s %d %s %d", dow[days % 7], d, mon[mo - 1], y);

	if (k->analog) {
		int cx = c->w / 2, cy = (c->h - 2) / 2;
		int rx = cx - 2, ry = cy;

		for (i = 0; i < 60; i += 5) {
			int px = cx + sin60[i] * rx / 1000;
			int py = cy - sin60[(i + 15) % 60] * ry / 1000;

			steel_put(c, px, py, i % 15 ? 0xF9 : 0xFE,
				  STEEL_ATTR(STEEL_BLACK, STEEL_WHITE));
		}
		hand(c, cx, cy, rx, ry, (hh % 12) * 5 + mm / 12, 55, 0xDB,
		     STEEL_ATTR(STEEL_BLUE, STEEL_WHITE));
		hand(c, cx, cy, rx, ry, mm, 85, 0xB2,
		     STEEL_ATTR(STEEL_BLACK, STEEL_WHITE));
		hand(c, cx, cy, rx, ry, ss, 90, 0xFA,
		     STEEL_ATTR(STEEL_RED, STEEL_WHITE));
		steel_put(c, cx, cy, 0xFE, STEEL_ATTR(STEEL_BLACK, STEEL_WHITE));
		steel_puts(c, (c->w - (int)strlen(buf)) / 2, c->h - 1, buf, A_WIN);
	} else {
		snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hh, mm, ss);
		steel_puts(c, (c->w - 8) / 2, c->h / 2 - 1, buf,
			   STEEL_ATTR(STEEL_BLUE, STEEL_WHITE));
		snprintf(buf, sizeof(buf), "%s %d %s %d", dow[days % 7], d,
			 mon[mo - 1], y);
		steel_puts(c, (c->w - (int)strlen(buf)) / 2, c->h / 2 + 1, buf, A_WIN);
	}
}

static void clk_command(struct win *w, int id)
{
	struct clk *k = DESK_PRIV(w, struct clk);

	if (id == CK_ANALOG || id == CK_DIGITAL) {
		k->analog = id == CK_ANALOG;
		k->dirty = true;
		desk_set_client_size(w, k->analog ? 30 : 24, k->analog ? 15 : 5);
	}
}

const struct app app_clock = {
	.name = "clock",
	.title = "Clock",
	.icon = { " \xDA\xC4\xC4\xBF ", " \xC0\xC4\xC4\xD9 " },
	.icon_attr = { STEEL_ATTR(STEEL_BLACK, STEEL_WHITE),
		       STEEL_ATTR(STEEL_BLACK, STEEL_WHITE) },
	.cw = 30, .ch = 15,
	.flags = APP_SINGLE,
	.tick_ms = 500,			/* the wake-up repaints the face */
	.menus = menus, .nmenus = ARRAY_SIZE(menus),
	.open = clk_open,
	.paint = clk_paint,
	.command = clk_command,
};
