// SPDX-License-Identifier: GPL-2.0-only
/* steel pixdev: draw the cell surface with smooth glyphs, see pixdev.h. */
#include <hix/kernel.h>
#include <steel/pixdev.h>

static const u32 palette[16] = {
	GFX_RGB(0x00, 0x00, 0x00), GFX_RGB(0x00, 0x00, 0x80),
	GFX_RGB(0x00, 0x80, 0x00), GFX_RGB(0x00, 0x80, 0x80),
	GFX_RGB(0x80, 0x00, 0x00), GFX_RGB(0x80, 0x00, 0x80),
	GFX_RGB(0x80, 0x80, 0x00), GFX_RGB(0xC0, 0xC0, 0xC0),
	GFX_RGB(0x80, 0x80, 0x80), GFX_RGB(0x00, 0x00, 0xFF),
	GFX_RGB(0x00, 0xFF, 0x00), GFX_RGB(0x00, 0xFF, 0xFF),
	GFX_RGB(0xFF, 0x00, 0x00), GFX_RGB(0xFF, 0x00, 0xFF),
	GFX_RGB(0xFF, 0xFF, 0x00), GFX_RGB(0xFF, 0xFF, 0xFF),
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

static void cursor_restore(struct pixdev *d)
{
	int i, j;

	if (!d->under_valid)
		return;
	for (j = 0; j < PIX_CUR_H; j++)
		for (i = 0; i < PIX_CUR_W; i++)
			gfx_plot(&d->fb, d->under_x + i, d->under_y + j,
				 d->under[j * PIX_CUR_W + i], 255);
	d->under_valid = false;
}

static void cursor_draw(struct pixdev *d)
{
	int i, j;

	if (!d->cur_visible)
		return;
	d->under_x = d->cur_x;
	d->under_y = d->cur_y;
	for (j = 0; j < PIX_CUR_H; j++) {
		for (i = 0; i < PIX_CUR_W; i++) {
			int x = d->cur_x + i, y = d->cur_y + j;
			char a = cursor_art[j][i];

			d->under[j * PIX_CUR_W + i] =
				x >= 0 && x < d->fb.w && y >= 0 && y < d->fb.h ?
				d->fb.px[y * d->fb.stride + x] : 0;
			if (a == 'X')
				gfx_plot(&d->fb, x, y, GFX_RGB(0, 0, 0), 255);
			else if (a == 'O')
				gfx_plot(&d->fb, x, y, GFX_RGB(255, 255, 255), 255);
		}
	}
	d->under_valid = true;
}

void pixdev_cursor(struct pixdev *d, int x, int y, bool visible)
{
	cursor_restore(d);
	d->cur_x = x;
	d->cur_y = y;
	d->cur_visible = visible;
	cursor_draw(d);
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

static void draw_cell(struct pixdev *d, int cx, int cy, steel_cell_t cell)
{
	int x = d->off_x + cx * PIX_CELL_W, y = d->off_y + cy * PIX_CELL_H;
	u32 fg = palette[STEEL_AT(cell) & 15];
	u32 bg = palette[(STEEL_AT(cell) >> 4) & 15];
	int ch = STEEL_CH(cell);

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

void pixdev_init(struct pixdev *d, const struct gfx_canvas *fb)
{
	memset(d, 0, sizeof(*d));
	d->fb = *fb;
	gfx_clip_reset(&d->fb);
	d->off_x = pixdev_off_x(fb);
	d->off_y = pixdev_off_y(fb);
	gfx_fill(&d->fb, 0, 0, fb->w, fb->h, palette[3]);
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
	int cx, cy, i;

	cursor_restore(d);
	for (cy = 0; cy < PIX_ROWS; cy++) {
		for (cx = 0; cx < PIX_COLS; cx++) {
			int n = cy * PIX_COLS + cx;
			steel_cell_t cell = cells[n];
			int px = d->off_x + cx * PIX_CELL_W;
			int py = d->off_y + cy * PIX_CELL_H;

			if (d->have_prev && d->prev[n] == cell &&
			    !(STEEL_IS_HOLE(cell) &&
			      client_dirty_at(d, cell, px, py)))
				continue;
			draw_cell(d, cx, cy, cell);
			d->prev[n] = cell;
		}
	}
	d->have_prev = true;
	for (i = 0; i < PIX_CLIENTS; i++)
		d->client[i].dx0 = d->client[i].dx1 = d->client[i].dy0 =
			d->client[i].dy1 = 0;
	cursor_draw(d);
}
