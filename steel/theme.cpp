// SPDX-License-Identifier: GPL-2.0-only
/*
 * steel theme: Argent, the look of the pixel desktop. See steel/theme.h.
 *
 * Every function here draws one grid cell (12x28 pixels) or a clipped slice
 * of something larger: a pinstriped title bar, a rounded button and a
 * centred label are all drawn once per cell they cover, with the clip
 * rectangle set to that cell. That keeps the display's redraw logic simple
 * (it only compares cells) and makes windows cover each other correctly.
 *
 * Integer arithmetic only: the kernel is built without an FPU.
 */
extern "C" {
#include <hix/kernel.h>
#include <steel/pixdev.h>
#include <steel/theme.h>

extern const char *const theme_icon_names[];
extern const u32 theme_icon_px[][48 * 48];
}

namespace {

constexpr int CW = PIX_CELL_W;
constexpr int CH = PIX_CELL_H;
constexpr int ICON_PX = 48;
constexpr int ICON_COUNT = 13;

constexpr u32 rgb(unsigned r, unsigned g, unsigned b)
{
	return GFX_RGB(r, g, b);
}

constexpr u32 PLATINUM = rgb(221, 221, 221);
constexpr u32 PLATINUM_OFF = rgb(236, 236, 236);
constexpr u32 LIGHT = rgb(255, 255, 255);
constexpr u32 STRIPE = rgb(188, 190, 194);
constexpr u32 SHADE = rgb(176, 176, 176);
constexpr u32 INSET = rgb(128, 128, 132);
constexpr u32 EDGE = rgb(84, 86, 92);
constexpr u32 EDGE_OFF = rgb(150, 150, 154);
constexpr u32 INK = rgb(28, 28, 32);
constexpr u32 INK_OFF = rgb(120, 120, 124);
constexpr u32 ACCENT = rgb(47, 93, 168);
constexpr u32 ACCENT_DARK = rgb(30, 62, 120);

u32 lerp(u32 a, u32 b, unsigned int t)			/* t: 0..255 */
{
	return gfx_mix(a, b, t);
}

/* ---- the wallpaper ---------------------------------------------------- */

u32 wall_px(int x, int y)
{
	constexpr u32 top_left = rgb(26, 74, 102);
	constexpr u32 bottom_right = rgb(64, 146, 146);
	int t = (x * 256 / 1024 + y * 256 / 768) / 2;
	u32 c;

	if (t < 0)
		t = 0;
	if (t > 255)
		t = 255;
	c = lerp(top_left, bottom_right, (unsigned int)t);
	/* a faint lattice of diamonds, 48 pixels apart */
	if (((x + y) % 48) == 0 || ((x - y + 4800) % 48) == 0)
		c = lerp(c, LIGHT, 18);
	/* a gentle shade towards the bottom, where the minimised icons sit */
	if (y > 640)
		c = lerp(c, rgb(0, 20, 30), (unsigned int)((y - 640) * 40 / 128));
	return c;
}

/* ---- small helpers ----------------------------------------------------- */

struct Cell {
	struct gfx_canvas *fb;
	int x, y;				/* top left pixel */
	int cx, cy;
	const steel_cell_t *cells;
	int cols, rows;
	steel_cell_t cell;

	unsigned int role() const { return TR_ID(STEEL_ROLE(cell)); }
	unsigned int aux() const { return STEEL_AUX(cell); }
	u32 fg() const { return pixdev_color(STEEL_AT(cell) & 15); }
	u32 bg() const { return pixdev_color((STEEL_AT(cell) >> 4) & 15); }

	void fill(int dx, int dy, int w, int h, u32 c) const
	{
		gfx_fill(fb, x + dx, y + dy, w, h, c);
	}
	void clip() const { gfx_clip_set(fb, x, y, CW, CH); }
	void unclip() const { gfx_clip_reset(fb); }
};

/* a character of the cell font in colour @c */
void glyph(const Cell &c, int ch, u32 color)
{
	const u8 *g = gfx_font_cell.alpha + ch * CW * CH;

	gfx_mask(c.fb, c.x, c.y, g, CW, CH, CW, color, 255);
}

steel_cell_t at(const Cell &c, int cx, int cy)
{
	if (cx < 0 || cy < 0 || cx >= c.cols || cy >= c.rows)
		return 0;
	return c.cells[cy * c.cols + cx];
}

/* the extent of the run of cells with the same run key as this one */
void run_extent(const Cell &c, int &first, int &last)
{
	unsigned int key = theme_run_key(c.cell);

	first = last = c.cx;
	while (theme_run_key(at(c, first - 1, c.cy)) == key)
		first--;
	while (theme_run_key(at(c, last + 1, c.cy)) == key)
		last++;
}

/* the text of a run: the characters of its text cells, trimmed */
int run_text(const Cell &c, int first, int last, char *buf, int size)
{
	int n = 0, i, start = 0;
	unsigned int role = c.role();

	for (i = first; i <= last && n < size - 1; i++) {
		steel_cell_t k = at(c, i, c.cy);
		bool text = true;

		if (role == TR_BUTTON)
			text = BA_PART(STEEL_AUX(k)) == BP_BODY;
		if (text)
			buf[n++] = (char)STEEL_CH(k);
	}
	buf[n] = '\0';
	while (buf[start] == ' ')
		start++;
	while (n > start && buf[n - 1] == ' ')
		buf[--n] = '\0';
	if (start)
		memmove(buf, buf + start, (size_t)(n - start + 1));
	return n - start;
}

/* ---- title bars --------------------------------------------------------- */

void box_glyph(const Cell &c, bool active, int kind)
{
	int bx = c.x + 1, by = c.y + 9;

	if (!active)
		return;
	gfx_fill(c.fb, bx - 1, by - 1, 12, 12, PLATINUM);	/* clear halo */
	gfx_fill(c.fb, bx, by, 10, 10, EDGE);
	gfx_vgrad(c.fb, bx + 1, by + 1, 8, 8, LIGHT, SHADE);
	if (kind == TP_ZOOM)
		gfx_frame(c.fb, bx + 3, by + 3, 4, 4, EDGE);
	else if (kind == TP_COLLAPSE)
		gfx_fill(c.fb, bx + 2, by + 4, 6, 2, EDGE);
}

void draw_title(const Cell &c)
{
	unsigned int aux = c.aux(), part = TA_PART(aux);
	bool active = aux & TA_ACTIVE;
	int r;

	if (active) {
		c.fill(0, 0, CW, CH, PLATINUM);
		c.fill(0, 0, CW, 1, EDGE);
		c.fill(0, 1, CW, 1, LIGHT);
		if (part != TP_TEXT)
			for (r = 5; r < 23; r += 2) {
				c.fill(0, r, CW, 1, LIGHT);
				c.fill(0, r + 1, CW, 1, STRIPE);
			}
		c.fill(0, CH - 2, CW, 1, SHADE);
	} else {
		c.fill(0, 0, CW, CH, PLATINUM_OFF);
		c.fill(0, 0, CW, 1, EDGE_OFF);
		c.fill(0, 1, CW, 1, LIGHT);
	}
	c.fill(0, CH - 1, CW, 1, active ? INSET : EDGE_OFF);
	if (aux & TA_FIRST) {
		c.fill(0, 0, 1, CH, active ? EDGE : EDGE_OFF);
		c.fill(1, 1, 1, CH - 3, LIGHT);
	}
	if (aux & TA_LAST) {
		c.fill(CW - 1, 0, 1, CH, active ? EDGE : EDGE_OFF);
		c.fill(CW - 2, 1, 1, CH - 3, SHADE);
	}

	if (part == TP_CLOSE || part == TP_ZOOM || part == TP_COLLAPSE) {
		box_glyph(c, active, (int)part);
	} else if (part == TP_TEXT) {
		int first, last, n, w;
		char buf[48];

		run_extent(c, first, last);
		n = run_text(c, first, last, buf, sizeof(buf));
		if (n) {
			int span = (last - first + 1) * CW;
			int ty = c.y + (CH - gfx_font_uib.h) / 2 + 1;

			w = gfx_text_width(&gfx_font_uib, buf);
			if (w > span - 4)
				w = span - 4;
			c.clip();
			gfx_text(c.fb, &gfx_font_uib, (first - c.cx) * CW + c.x +
				 (span - w) / 2, ty, buf, active ? INK : INK_OFF);
			c.unclip();
		}
	}
}

/* ---- frames --------------------------------------------------------------- */

void draw_frame(const Cell &c)
{
	unsigned int aux = c.aux();
	bool active = aux & FA_ACTIVE;
	bool flat = aux & FA_FLAT;
	u32 edge = active || flat ? EDGE : EDGE_OFF;

	if (flat) {
		c.fill(0, 0, CW, CH, LIGHT);
		if (aux & FS_L)
			c.fill(0, 0, 1, CH, edge);
		if (aux & FS_R)
			c.fill(CW - 1, 0, 1, CH, edge);
		if (aux & FS_T)
			c.fill(0, 0, CW, 1, edge);
		if (aux & FS_B)
			c.fill(0, CH - 1, CW, 1, edge);
		return;
	}
	c.fill(0, 0, CW, CH, active ? PLATINUM : PLATINUM_OFF);
	if (aux & FS_L) {
		c.fill(0, 0, 1, CH, edge);
		c.fill(1, 0, 1, CH, LIGHT);
		c.fill(CW - 1, 0, 1, CH, active ? INSET : EDGE_OFF);	/* the client's edge */
	}
	if (aux & FS_R) {
		c.fill(CW - 1, 0, 1, CH, edge);
		c.fill(CW - 2, 0, 1, CH, SHADE);
		c.fill(0, 0, 1, CH, active ? INSET : EDGE_OFF);
	}
	if (aux & FS_B) {
		/* a status bar: a lighter strip with the client's edge above it */
		c.fill(0, 0, CW, 1, active ? INSET : EDGE_OFF);
		c.fill(0, CH - 1, CW, 1, edge);
		c.fill(0, CH - 2, CW, 1, SHADE);
		if (aux & FS_L) {
			c.fill(0, 0, 1, CH, edge);
			c.fill(1, 1, 1, CH - 3, LIGHT);
		}
		if (aux & FS_R) {
			c.fill(CW - 1, 0, 1, CH, edge);
			c.fill(CW - 2, 1, 1, CH - 3, SHADE);
		}
	}
}

/* ---- menus ----------------------------------------------------------------- */

void draw_menubar(const Cell &c)
{
	bool sel = c.aux() & MB_SELECTED;
	int ch = STEEL_CH(c.cell);

	if (sel) {
		c.fill(0, 0, CW, CH, ACCENT);
	} else {
		gfx_vgrad(c.fb, c.x, c.y, CW, CH - 1, rgb(250, 250, 250), rgb(232, 232, 232));
		c.fill(0, CH - 1, CW, 1, SHADE);
	}
	if (ch != ' ' && ch != 0)
		glyph(c, ch, sel ? LIGHT : INK);
}

void draw_drop(const Cell &c)
{
	unsigned int kind = c.aux();
	int ch = STEEL_CH(c.cell);
	u32 text = INK;

	c.fill(0, 0, CW, CH, LIGHT);
	if (kind == DP_SELECTED) {
		c.fill(0, 0, CW, CH, ACCENT);
		text = LIGHT;
	} else if (kind == DP_DISABLED) {
		text = INK_OFF;
	} else if (kind == DP_SEPARATOR) {
		c.fill(0, CH / 2 - 1, CW, 1, SHADE);
		c.fill(0, CH / 2, CW, 1, LIGHT);
		return;
	}
	if (ch != ' ' && ch != 0)
		glyph(c, ch, text);
}

/* ---- buttons ----------------------------------------------------------------- */

void draw_button(const Cell &c)
{
	unsigned int aux = c.aux();
	bool dflt = aux & BA_DEFAULT;
	int first, last, n;
	char buf[40];
	int x0, w, y0 = c.y + 3, h = CH - 6;

	c.fill(0, 0, CW, CH, aux & BA_ON_GRAY ? PLATINUM : LIGHT);
	run_extent(c, first, last);
	x0 = c.x + (first - c.cx) * CW + 2;
	w = (last - first + 1) * CW - 4;

	c.clip();
	if (dflt)
		gfx_rrect(c.fb, x0 - 2, y0 - 2, w + 4, h + 4, 9, ACCENT, 255);
	gfx_rrect(c.fb, x0, y0, w, h, 7, EDGE, 255);
	gfx_rrect(c.fb, x0 + 1, y0 + 1, w - 2, h - 2, 6, rgb(232, 232, 234), 255);
	gfx_rrect(c.fb, x0 + 2, y0 + 2, w - 4, (h - 4) / 2, 5, LIGHT, 150);
	n = run_text(c, first, last, buf, sizeof(buf));
	if (n) {
		const struct gfx_font *f = dflt ? &gfx_font_uib : &gfx_font_ui;
		int tw = gfx_text_width(f, buf);

		gfx_text(c.fb, f, x0 + (w - tw) / 2, y0 + (h - f->h) / 2 + (dflt ? 0 : 1),
			 buf, INK);
	}
	c.unclip();
}

/* ---- icons and labels ---------------------------------------------------------- */

void draw_icon(const Cell &c)
{
	int col = (STEEL_CH(c.cell) >> 4) & 7, row = STEEL_CH(c.cell) & 15;
	bool desk = STEEL_CH(c.cell) & TI_DESKTOP;
	unsigned int id = c.aux();
	int ox = (col - 1) * CW;		/* the picture is 4 of the 6 cells wide */
	int oy = row * CH - (TI_CELLS_H * CH - ICON_PX) / 2;
	const u32 *src;
	int sy;

	if (desk)
		theme_wallpaper(c.fb, c.x, c.y, CW, CH);
	else
		c.fill(0, 0, CW, CH, LIGHT);
	if (id >= ICON_COUNT)
		id = 0;
	src = theme_icon_px[id];
	for (sy = 0; sy < CH; sy++) {
		int iy = oy + sy, sx;

		if (iy < 0 || iy >= ICON_PX)
			continue;
		for (sx = 0; sx < CW; sx++) {
			int ix = ox + sx;
			u32 p;
			unsigned int a;

			if (ix < 0 || ix >= ICON_PX)
				continue;
			p = src[iy * ICON_PX + ix];
			a = p >> 24;
			if (a)
				gfx_plot(c.fb, c.x + sx, c.y + sy, p & 0xFFFFFF, a);
		}
	}
}

/* the text of each label seen in full, so that a covered label keeps its place */
struct LabelMemo {
	bool used;
	int cx, cy;
	char text[24];
};
LabelMemo label_memo[24];
unsigned int label_next;

LabelMemo *memo_find(int cx, int cy)
{
	unsigned int i;

	for (i = 0; i < sizeof(label_memo) / sizeof(label_memo[0]); i++)
		if (label_memo[i].used && label_memo[i].cx == cx && label_memo[i].cy == cy)
			return &label_memo[i];
	return nullptr;
}

void draw_label(const Cell &c)
{
	unsigned int aux = c.aux();
	bool sel = aux & TL_SELECTED, desk = aux & TL_ON_DESKTOP;
	int first, last, n, span = TL_SPAN * CW, tw, tx, ty;
	int x0 = c.cx - (int)TL_GET_INDEX(aux);		/* first cell of the whole label */
	char buf[24];
	const struct gfx_font *f = &gfx_font_ui;
	LabelMemo *m;

	if (desk)
		theme_wallpaper(c.fb, c.x, c.y, CW, CH);
	else
		c.fill(0, 0, CW, CH, LIGHT);
	run_extent(c, first, last);
	n = run_text(c, first, last, buf, sizeof(buf));
	m = memo_find(x0, c.cy);
	if (first == x0 && last == x0 + TL_SPAN - 1) {
		if (!m) {		/* a new label: take the oldest slot */
			m = &label_memo[label_next++ % (sizeof(label_memo) /
							sizeof(label_memo[0]))];
			m->used = true;
			m->cx = x0;
			m->cy = c.cy;
		}
		strlcpy(m->text, buf, sizeof(m->text));
	} else if (m) {
		strlcpy(buf, m->text, sizeof(buf));
		n = (int)strlen(buf);
	}
	if (!n)
		return;
	tw = gfx_text_width(f, buf);
	if (tw > span - 4)
		tw = span - 4;
	tx = c.x + (x0 - c.cx) * CW + (span - tw) / 2;
	ty = c.y + (CH - f->h) / 2 + 1;

	c.clip();
	if (sel)
		gfx_rrect(c.fb, tx - 5, c.y + 3, tw + 10, CH - 6, 6, ACCENT, 255);
	if (desk && !sel)
		gfx_text(c.fb, f, tx + 1, ty + 1, buf, rgb(0, 16, 24));
	gfx_text(c.fb, f, tx, ty, buf, sel || desk ? LIGHT : INK);
	c.unclip();
}

} // namespace

/* ---- the C interface ---------------------------------------------------------------- */

extern "C" {

bool theme_is_wallpaper(steel_cell_t cell)
{
	return TR_ID(STEEL_ROLE(cell)) == TR_NONE && STEEL_AT(cell) == THEME_DESKTOP_ATTR &&
	       (STEEL_CH(cell) == ' ' || STEEL_CH(cell) == 0);
}

unsigned int theme_run_key(steel_cell_t cell)
{
	unsigned int role = TR_ID(STEEL_ROLE(cell)), aux = STEEL_AUX(cell);

	switch (role) {
	case TR_TITLE:
		return TA_PART(aux) == TP_TEXT ? 0x100u | (aux & TA_ACTIVE) : 0;
	case TR_BUTTON:
		return 0x200u | (aux & BA_DEFAULT);
	case TR_LABEL:
		return 0x300u | (aux & 3u);
	default:
		return 0;
	}
}

void theme_wallpaper(struct gfx_canvas *fb, int x, int y, int w, int h)
{
	int ix, iy;

	for (iy = y; iy < y + h; iy++)
		for (ix = x; ix < x + w; ix++)
			gfx_plot(fb, ix, iy, wall_px(ix, iy), 255);
}

void theme_draw_cell(struct gfx_canvas *fb, int px, int py, int cx, int cy,
		     const steel_cell_t *cells, int cols, int rows)
{
	Cell c = { fb, px, py, cx, cy, cells, cols, rows, cells[cy * cols + cx] };

	switch (c.role()) {
	case TR_TITLE:
		draw_title(c);
		break;
	case TR_FRAME:
		draw_frame(c);
		break;
	case TR_MENUBAR:
		draw_menubar(c);
		break;
	case TR_BUTTON:
		draw_button(c);
		break;
	case TR_DROP:
		draw_drop(c);
		break;
	case TR_ICON:
		draw_icon(c);
		break;
	case TR_LABEL:
		draw_label(c);
		break;
	default:
		break;
	}
}

void theme_shadow(struct gfx_canvas *fb, int px, int py, unsigned int bits)
{
	constexpr int depth = 7;
	int i, j;

	for (j = 0; j < CH; j++) {
		for (i = 0; i < CW; i++) {
			unsigned int a = 0, b = 0;

			if ((bits & TR_SHADOW_R) && (bits & TR_SHADOW_B)) {
				/* the corner: fades with the distance from the window */
				if (i < depth && j < depth)
					a = (unsigned int)(depth - (i > j ? i : j)) * 11;
			} else if (bits & TR_SHADOW_R) {
				if (i < depth)
					a = (unsigned int)(depth - i) * 11;
			} else if (j < depth) {
				b = (unsigned int)(depth - j) * 11;
			}
			if (a < b)
				a = b;
			if (a)
				gfx_plot(fb, px + i, py + j, rgb(0, 0, 0), a);
		}
	}
}

int theme_icon_id(const char *name)
{
	int i;

	for (i = 1; i < ICON_COUNT; i++)
		if (!strcmp(theme_icon_names[i], name))
			return i;
	return 0;
}

} // extern "C"
