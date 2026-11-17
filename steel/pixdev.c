// SPDX-License-Identifier: GPL-2.0-only
/* steel pixdev: draw the cell surface with smooth glyphs, see pixdev.h. */
#include <hix/kernel.h>
#include <steel/pixdev.h>
#include <steel/theme.h>

/* the Argent palette: the 16 attribute colours, softened */
static const u32 palette[16] = {
	GFX_RGB(0x1C, 0x1C, 0x20), GFX_RGB(0x2F, 0x5D, 0xA8),
	GFX_RGB(0x2E, 0x8B, 0x57), GFX_RGB(0x2B, 0x7A, 0x8C),
	GFX_RGB(0xC0, 0x39, 0x2B), GFX_RGB(0x8E, 0x44, 0xAD),
	GFX_RGB(0xB7, 0x79, 0x1F), GFX_RGB(0xDC, 0xDC, 0xDC),
	GFX_RGB(0x7A, 0x7A, 0x7A), GFX_RGB(0x5B, 0x9B, 0xEF),
	GFX_RGB(0x5C, 0xBF, 0x6A), GFX_RGB(0x5C, 0xC8, 0xD8),
	GFX_RGB(0xEF, 0x6B, 0x5B), GFX_RGB(0xC7, 0x7D, 0xDB),
	GFX_RGB(0xF2, 0xC9, 0x4C), GFX_RGB(0xFF, 0xFF, 0xFF),
};

u32 pixdev_color(int idx)
{
	return palette[idx & 15];
}

/* ---- the pointer ------------------------------------------------------- */

static const char *const cursor_art[PIX_CUR_H] = {
	"X...........",
	"XX..........",
	"XOX.........",
	"XOOX........",
	"XOOOX.......",
	"XOOOOX......",
	"XOOOOOX.....",
	"XOOOOOOX....",
	"XOOOOOOOX...",
	"XOOOOOOOOX..",
	"XOOOOOXXXXX.",
	"XOOXOOX.....",
	"XOX.XOOX....",
	"XX..XOOX....",
	"X....XOOX...",
	".....XOOX...",
	"......XOOX..",
	"......XOOX..",
	".......XX...",
};

/* ---- dirty spans and the real screen ------------------------------------ */

/* remember that the rectangle changed in the back buffer */
static void mark(struct pixdev *d, int x, int y, int w, int h)
{
	int x0 = max(x, 0), x1 = min(x + w, d->fb.w);
	int y0 = max(y, 0), y1 = min(y + h, min(d->fb.h, PIX_MAX_H));

	if (x0 >= x1)
		return;
	for (; y0 < y1; y0++) {
		if (d->span0[y0] >= d->span1[y0]) {
			d->span0[y0] = (s16)x0;
			d->span1[y0] = (s16)x1;
		} else {
			d->span0[y0] = (s16)min((int)d->span0[y0], x0);
			d->span1[y0] = (s16)max((int)d->span1[y0], x1);
		}
	}
}

/* 3 steps of shadow, 1 = nearest to the window */
static u32 shade(u32 c, int step)
{
	if (step == 1)
		return ((c >> 1) & 0x7F7F7F) + ((c >> 3) & 0x1F1F1F);
	if (step == 2)
		return c - ((c >> 2) & 0x3F3F3F);
	return c - ((c >> 3) & 0x1F1F1F);
}

static void draw_float(struct pixdev *d)
{
	const struct gfx_canvas *s = &d->screen;
	int x0 = d->fl_x, y0 = d->fl_y, w = d->fl_w, h = d->fl_h, y, i;

	/* the shadow first: the background, darker, right and below the window */
	for (y = max(y0 + 4, 0); y < min(y0 + h + PIX_FLOAT_SHADOW, s->h); y++) {
		int xa = x0 + (y < y0 + h ? w : 4);
		int xb = x0 + w + PIX_FLOAT_SHADOW;
		int x;

		for (x = max(xa, 0); x < min(xb, s->w); x++) {
			int dist = max(x - (x0 + w - 1), y - (y0 + h - 1));
			int step = dist <= 2 ? 1 : dist <= 4 ? 2 : 3;

			s->px[y * s->stride + x] =
				shade(d->fb.px[y * d->fb.stride + x], step);
		}
	}
	for (i = 0; i < h; i++) {
		int y1 = y0 + i, xa = max(x0, 0), xb = min(x0 + w, s->w);

		if (y1 < 0 || y1 >= s->h || xa >= xb)
			continue;
		memcpy(&s->px[y1 * s->stride + xa],
		       &d->sprite[i * w + (xa - x0)], (size_t)(xb - xa) * 4);
	}
}

static void draw_pointer(struct pixdev *d)
{
	int i, j;

	if (!d->cur_visible)
		return;
	for (j = 0; j < PIX_CUR_H; j++) {
		for (i = 0; i < PIX_CUR_W; i++) {
			char a = cursor_art[j][i];

			if (a == 'X')
				gfx_plot(&d->screen, d->cur_x + i, d->cur_y + j,
					 GFX_RGB(0, 0, 0), 255);
			else if (a == 'O')
				gfx_plot(&d->screen, d->cur_x + i, d->cur_y + j,
					 GFX_RGB(255, 255, 255), 255);
		}
	}
}

/*
 * Bring the screen up to date: copy the changed spans of the back buffer,
 * lay the floating window over them, then the pointer.
 */
static void copy_span(struct pixdev *d, int y, int x0, int x1)
{
	const struct gfx_canvas *s = &d->screen;

	if (x0 < x1)
		memcpy(&s->px[y * s->stride + x0], &d->fb.px[y * d->fb.stride + x0],
		       (size_t)(x1 - x0) * 4);
}

static void notify(struct pixdev *d, int x0, int y0, int x1, int y1)
{
	if (!d->update)
		return;
	x0 = max(x0, 0);
	y0 = max(y0, 0);
	x1 = min(x1, d->screen.w);
	y1 = min(y1, d->screen.h);
	if (x0 < x1 && y0 < y1)
		d->update(d->update_ctx, x0, y0, x1 - x0, y1 - y0);
}

static void flush(struct pixdev *d)
{
	bool hit_float = d->fl_redo;
	int y, rows = min(d->screen.h, PIX_MAX_H);
	int bx0 = d->screen.w, by0 = d->screen.h, bx1 = 0, by1 = 0;

	if (d->fl_on && !hit_float) {
		for (y = 0; y < rows && !hit_float; y++)
			hit_float = d->span0[y] < d->span1[y] &&
				    y >= d->fl_y &&
				    y < d->fl_y + d->fl_h + PIX_FLOAT_SHADOW &&
				    d->span1[y] > d->fl_x &&
				    d->span0[y] < d->fl_x + d->fl_w + PIX_FLOAT_SHADOW;
	}
	for (y = 0; y < rows; y++) {
		int x0 = d->span0[y], x1 = d->span1[y];

		if (x0 >= x1)
			continue;
		if (d->fl_on && hit_float && y >= d->fl_y && y < d->fl_y + d->fl_h) {
			/* the sprite is about to cover this part: do not copy it */
			copy_span(d, y, x0, min(x1, d->fl_x));
			copy_span(d, y, max(x0, d->fl_x + d->fl_w), x1);
		} else {
			copy_span(d, y, x0, x1);
		}
		bx0 = min(bx0, x0);
		bx1 = max(bx1, x1);
		by0 = min(by0, y);
		by1 = max(by1, y + 1);
		d->span0[y] = d->span1[y] = 0;
	}
	if (d->fl_on && hit_float) {
		draw_float(d);
		bx0 = min(bx0, d->fl_x);
		by0 = min(by0, d->fl_y);
		bx1 = max(bx1, d->fl_x + d->fl_w + PIX_FLOAT_SHADOW);
		by1 = max(by1, d->fl_y + d->fl_h + PIX_FLOAT_SHADOW);
	}
	d->fl_redo = false;
	draw_pointer(d);
	if (d->cur_visible) {
		bx0 = min(bx0, d->cur_x);
		by0 = min(by0, d->cur_y);
		bx1 = max(bx1, d->cur_x + PIX_CUR_W);
		by1 = max(by1, d->cur_y + PIX_CUR_H);
	}
	notify(d, bx0, by0, bx1, by1);
}

void pixdev_cursor(struct pixdev *d, int x, int y, bool visible)
{
	mark(d, d->cur_x, d->cur_y, PIX_CUR_W, PIX_CUR_H);
	d->cur_x = x;
	d->cur_y = y;
	d->cur_visible = visible;
	flush(d);
}

/* ---- line and block characters ---------------------------------------- */

/* arms of the code page 437 box characters 0xB3..0xDA: up, down, left, right;
 * 0 none, 1 single, 2 double */
static const char box_arms[0xDA - 0xB3 + 1][5] = {
	"1100", "1110", "1120", "2210", "0210", "0120", "2220", "2200",
	"0220", "2020", "2010", "1020", "0110", "1001", "1011", "0111",
	"1101", "0011", "1111", "1102", "2201", "2002", "0202", "2022",
	"0222", "2202", "0022", "2222", "1022", "2011", "0122", "0211",
	"2001", "1002", "0102", "0201", "2211", "1122", "1010", "0101",
};

static void draw_box(struct pixdev *d, int x, int y, u32 fg, int ch)
{
	const char *arms = box_arms[ch - 0xB3];
	int cx = x + PIX_CELL_W / 2, cy = y + PIX_CELL_H / 2;
	int up = arms[0] - '0', down = arms[1] - '0';
	int left = arms[2] - '0', right = arms[3] - '0';
	int vspan = up == 2 || down == 2 ? 4 : 1;	/* half width of the vertical part */
	int hspan = left == 2 || right == 2 ? 4 : 1;
	int k;

	/* vertical arms: single = one 2 px bar, double = two bars */
	for (k = 0; k < 2; k++) {
		int style = k ? down : up;
		int y0 = k ? cy - hspan : y;
		int y1 = k ? y + PIX_CELL_H : cy + hspan;

		if (style == 1)
			gfx_fill(&d->fb, cx - 1, y0, 2, y1 - y0, fg);
		else if (style == 2) {
			gfx_fill(&d->fb, cx - 4, y0, 2, y1 - y0, fg);
			gfx_fill(&d->fb, cx + 2, y0, 2, y1 - y0, fg);
		}
	}
	for (k = 0; k < 2; k++) {
		int style = k ? right : left;
		int x0 = k ? cx - vspan : x;
		int x1 = k ? x + PIX_CELL_W : cx + vspan;

		if (style == 1)
			gfx_fill(&d->fb, x0, cy - 1, x1 - x0, 2, fg);
		else if (style == 2) {
			gfx_fill(&d->fb, x0, cy - 4, x1 - x0, 2, fg);
			gfx_fill(&d->fb, x0, cy + 2, x1 - x0, 2, fg);
		}
	}
}

static void draw_base(struct pixdev *d, int cx, int cy, steel_cell_t cell,
		      const steel_cell_t *cells);

static void draw_cell(struct pixdev *d, int cx, int cy, steel_cell_t cell,
		      const steel_cell_t *cells)
{
	unsigned int shadow = STEEL_ROLE(cell) & (TR_SHADOW_R | TR_SHADOW_B);
	int x = d->off_x + cx * PIX_CELL_W, y = d->off_y + cy * PIX_CELL_H;

	draw_base(d, cx, cy, cell, cells);
	if (shadow)
		theme_shadow(&d->fb, x, y, shadow);
}

static void draw_base(struct pixdev *d, int cx, int cy, steel_cell_t cell,
		      const steel_cell_t *cells)
{
	int x = d->off_x + cx * PIX_CELL_W, y = d->off_y + cy * PIX_CELL_H;
	u32 fg = palette[STEEL_AT(cell) & 15];
	u32 bg = palette[(STEEL_AT(cell) >> 4) & 15];
	int ch = STEEL_CH(cell);

	if (TR_ID(STEEL_ROLE(cell))) {
		theme_draw_cell(&d->fb, x, y, cx, cy, cells, PIX_COLS, PIX_ROWS);
		return;
	}
	if (theme_is_wallpaper(cell)) {
		theme_wallpaper(&d->fb, x, y, PIX_CELL_W, PIX_CELL_H);
		return;
	}
	if (STEEL_IS_HOLE(cell)) {
		const struct pix_client *c = &d->client[STEEL_AT(cell) - 0xA0];
		struct gfx_canvas src;
		int sx = x - c->x, sy = y - c->y;

		gfx_fill(&d->fb, x, y, PIX_CELL_W, PIX_CELL_H, GFX_RGB(0, 0, 0));
		if (!c->used)
			return;
		gfx_canvas_init(&src, c->w, c->h, (u32 *)c->px);
		src.stride = c->stride;
		gfx_blit(&d->fb, x, y, &src, sx, sy, PIX_CELL_W, PIX_CELL_H);
		return;
	}
	gfx_fill(&d->fb, x, y, PIX_CELL_W, PIX_CELL_H, bg);
	if (ch == ' ' || ch == 0)
		return;
	if (ch >= 0xB0 && ch <= 0xB2) {			/* shades */
		static const unsigned int level[3] = { 64, 128, 192 };

		gfx_fill(&d->fb, x, y, PIX_CELL_W, PIX_CELL_H,
			 gfx_mix(bg, fg, level[ch - 0xB0]));
	} else if (ch >= 0xB3 && ch <= 0xDA) {
		draw_box(d, x, y, fg, ch);
	} else if (ch == 0xDB) {
		gfx_fill(&d->fb, x, y, PIX_CELL_W, PIX_CELL_H, fg);
	} else if (ch == 0xDC) {
		gfx_fill(&d->fb, x, y + PIX_CELL_H / 2, PIX_CELL_W, PIX_CELL_H / 2, fg);
	} else if (ch == 0xDD) {
		gfx_fill(&d->fb, x, y, PIX_CELL_W / 2, PIX_CELL_H, fg);
	} else if (ch == 0xDE) {
		gfx_fill(&d->fb, x + PIX_CELL_W / 2, y, PIX_CELL_W / 2, PIX_CELL_H, fg);
	} else if (ch == 0xDF) {
		gfx_fill(&d->fb, x, y, PIX_CELL_W, PIX_CELL_H / 2, fg);
	} else {
		const u8 *g = gfx_font_cell.alpha + ch * PIX_CELL_W * PIX_CELL_H;

		gfx_mask(&d->fb, x, y, g, PIX_CELL_W, PIX_CELL_H, PIX_CELL_W, fg, 255);
	}
}

/* ---- the device ---------------------------------------------------------- */

void pixdev_init(struct pixdev *d, const struct gfx_canvas *fb, u32 *back,
		 u32 *sprite)
{
	/* a backend may install the update hook before the first frame */
	void (*update)(void *ctx, int x, int y, int w, int h) = d->update;
	void *update_ctx = d->update_ctx;

	memset(d, 0, sizeof(*d));
	d->update = update;
	d->update_ctx = update_ctx;
	d->screen = *fb;
	gfx_canvas_init(&d->fb, fb->w, fb->h, back);
	d->sprite = sprite;
	d->off_x = pixdev_off_x(fb);
	d->off_y = pixdev_off_y(fb);
	theme_wallpaper(&d->fb, 0, 0, fb->w, fb->h);
	mark(d, 0, 0, fb->w, fb->h);
	flush(d);
}

/* ---- the floating window ------------------------------------------------ */

int pixdev_float(struct pixdev *d, const steel_cell_t *cells, int cx, int cy,
		 int cw, int ch, int px, int py)
{
	bool same = d->fl_on && d->fl_cx == cx && d->fl_cy == cy &&
		    d->fl_cw == cw && d->fl_ch == ch;
	int x, y;

	if (cx < 0 || cy < 0 || cw <= 0 || ch <= 0 || cx + cw > PIX_COLS ||
	    cy + ch > PIX_ROWS)
		return -EINVAL;
	for (y = 0; same && y < ch; y++)
		for (x = 0; x < cw; x++)
			if (d->fl_cells[(cy + y) * PIX_COLS + cx + x] !=
			    cells[(cy + y) * PIX_COLS + cx + x])
				same = false;
	if (!same) {			/* draw the window's cells into the sprite */
		/* draw through the same code, with the canvas swapped for a while */
		struct gfx_canvas saved = d->fb;
		int ox = d->off_x, oy = d->off_y;

		gfx_canvas_init(&d->fb, cw * PIX_CELL_W, ch * PIX_CELL_H, d->sprite);
		d->off_x = -cx * PIX_CELL_W;
		d->off_y = -cy * PIX_CELL_H;
		for (y = cy; y < cy + ch; y++)
			for (x = cx; x < cx + cw; x++)
				draw_cell(d, x, y, cells[y * PIX_COLS + x], cells);
		d->fb = saved;
		d->off_x = ox;
		d->off_y = oy;
		for (y = cy; y < cy + ch; y++)
			memcpy(&d->fl_cells[y * PIX_COLS + cx], &cells[y * PIX_COLS + cx],
			       (size_t)cw * sizeof(*cells));
	}
	if (d->fl_on)
		mark(d, d->fl_x, d->fl_y, d->fl_w + PIX_FLOAT_SHADOW,
		     d->fl_h + PIX_FLOAT_SHADOW);
	d->fl_on = true;
	d->fl_cx = cx;
	d->fl_cy = cy;
	d->fl_cw = cw;
	d->fl_ch = ch;
	d->fl_x = px;
	d->fl_y = py;
	d->fl_w = cw * PIX_CELL_W;
	d->fl_h = ch * PIX_CELL_H;
	mark(d, px, py, d->fl_w + PIX_FLOAT_SHADOW, d->fl_h + PIX_FLOAT_SHADOW);
	d->fl_redo = true;
	flush(d);
	return 0;
}

void pixdev_float_off(struct pixdev *d)
{
	if (!d->fl_on)
		return;
	mark(d, d->fl_x, d->fl_y, d->fl_w + PIX_FLOAT_SHADOW,
	     d->fl_h + PIX_FLOAT_SHADOW);
	d->fl_on = false;
	flush(d);
}

void pixdev_panel(struct pixdev *d, const struct steel_panel *p)
{
	if (d->have_panel && !memcmp(&d->panel, p, sizeof(*p)))
		return;
	d->panel = *p;
	d->have_panel = true;
	panel_draw(&d->fb, &d->panel);
	mark(d, 0, 0, d->fb.w, PANEL_TOP_H);
	flush(d);
}

int pixdev_client_set(struct pixdev *d, int idx, int x, int y, int w, int h,
		      const u32 *px, int stride)
{
	struct pix_client *c;

	if (idx < 0 || idx >= PIX_CLIENTS || w <= 0 || h <= 0 || !px)
		return -EINVAL;
	c = &d->client[idx];
	c->used = true;
	c->x = x;
	c->y = y;
	c->w = w;
	c->h = h;
	c->px = px;
	c->stride = stride;
	c->dx0 = c->dy0 = 0;
	c->dx1 = w;
	c->dy1 = h;
	return 0;
}

void pixdev_client_clear(struct pixdev *d, int idx)
{
	if (idx >= 0 && idx < PIX_CLIENTS) {
		d->client[idx].used = false;
		d->have_prev = false;		/* repaint what was underneath */
	}
}

void pixdev_client_dirty(struct pixdev *d, int idx, int x, int y, int w, int h)
{
	struct pix_client *c;

	if (idx < 0 || idx >= PIX_CLIENTS || !d->client[idx].used)
		return;
	c = &d->client[idx];
	if (c->dx0 >= c->dx1 || c->dy0 >= c->dy1) {	/* nothing dirty yet */
		c->dx0 = x;
		c->dy0 = y;
		c->dx1 = x + w;
		c->dy1 = y + h;
		return;
	}
	c->dx0 = min(c->dx0, x);
	c->dy0 = min(c->dy0, y);
	c->dx1 = max(c->dx1, x + w);
	c->dy1 = max(c->dy1, y + h);
}

static bool client_dirty_at(const struct pixdev *d, steel_cell_t cell, int px,
			    int py)
{
	const struct pix_client *c = &d->client[STEEL_AT(cell) - 0xA0];
	int x0 = px - c->x, y0 = py - c->y;

	if (!c->used || c->dx0 >= c->dx1)
		return false;
	return x0 < c->dx1 && x0 + PIX_CELL_W > c->dx0 &&
	       y0 < c->dy1 && y0 + PIX_CELL_H > c->dy0;
}

void pixdev_present(struct pixdev *d, const steel_cell_t *cells)
{
	static bool dirty[PIX_COLS * PIX_ROWS];
	int cx, cy, i;

	for (cy = 0; cy < PIX_ROWS; cy++) {
		for (cx = 0; cx < PIX_COLS; cx++) {
			int n = cy * PIX_COLS + cx;
			steel_cell_t cell = cells[n];
			int px = d->off_x + cx * PIX_CELL_W;
			int py = d->off_y + cy * PIX_CELL_H;

			dirty[n] = !d->have_prev || d->prev[n] != cell ||
				   (STEEL_IS_HOLE(cell) &&
				    client_dirty_at(d, cell, px, py));
		}
		/* a text run (title, button, label) is redrawn as a whole */
		for (cx = 0; cx < PIX_COLS; cx++) {
			unsigned int key = theme_run_key(cells[cy * PIX_COLS + cx]);
			int end = cx;
			bool any = false;

			if (!key)
				continue;
			while (end < PIX_COLS &&
			       theme_run_key(cells[cy * PIX_COLS + end]) == key) {
				any |= dirty[cy * PIX_COLS + end];
				end++;
			}
			if (any)
				for (i = cx; i < end; i++)
					dirty[cy * PIX_COLS + i] = true;
			cx = end - 1;
		}
		for (cx = 0; cx < PIX_COLS; cx++) {
			int n = cy * PIX_COLS + cx;

			if (!dirty[n])
				continue;
			draw_cell(d, cx, cy, cells[n], cells);
			mark(d, d->off_x + cx * PIX_CELL_W, d->off_y + cy * PIX_CELL_H,
			     PIX_CELL_W, PIX_CELL_H);
			d->prev[n] = cells[n];
		}
	}
	d->have_prev = true;
	for (i = 0; i < PIX_CLIENTS; i++)
		d->client[i].dx0 = d->client[i].dx1 = d->client[i].dy0 =
			d->client[i].dy1 = 0;
	flush(d);
}
