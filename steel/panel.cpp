// SPDX-License-Identifier: GPL-2.0-only
/*
 * steel panel: draws the top and bottom bars, see steel/panel.h.
 *
 * Platinum like the windows, but flat strips: a white highlight on top, a
 * dark line at the inner edge, rounded bevelled buttons for the taskbar.
 */
extern "C" {
#include <hix/kernel.h>
#include <steel/panel.h>

extern const u32 theme_icon_px[][48 * 48];
}

namespace {

constexpr u32 rgb(unsigned r, unsigned g, unsigned b)
{
	return GFX_RGB(r, g, b);
}

constexpr u32 PLATINUM = rgb(221, 221, 221);
constexpr u32 TOP_A = rgb(238, 238, 238);
constexpr u32 TOP_B = rgb(206, 206, 210);
constexpr u32 LIGHT = rgb(255, 255, 255);
constexpr u32 EDGE = rgb(84, 86, 92);
constexpr u32 SHADE = rgb(160, 160, 164);
constexpr u32 INK = rgb(28, 28, 32);
constexpr u32 INK_OFF = rgb(110, 110, 116);
constexpr u32 ACCENT = rgb(47, 93, 168);
constexpr u32 ACCENT_DARK = rgb(30, 62, 120);
constexpr int ICON_SMALL = 16;
constexpr int ICON_COUNT = 13;
constexpr int TASK_H = 20;
constexpr int TASK_MIN_W = 44;
constexpr int TASK_MAX_W = 150;
constexpr int TASK_GAP = 4;

/* the 48x48 icon at 16x16, 3x3 boxes averaged with their alpha */
void small_icon(struct gfx_canvas *fb, int x, int y, int id, bool dim)
{
	const u32 *src;
	int ix, iy;

	if (id < 0 || id >= ICON_COUNT)
		id = 0;
	src = theme_icon_px[id];
	for (iy = 0; iy < ICON_SMALL; iy++) {
		for (ix = 0; ix < ICON_SMALL; ix++) {
			unsigned int a = 0, r = 0, g = 0, b = 0;
			int k;

			for (k = 0; k < 9; k++) {
				u32 p = src[(iy * 3 + k / 3) * 48 + ix * 3 + k % 3];
				unsigned int pa = p >> 24;

				a += pa;
				r += GFX_R(p) * pa;
				g += GFX_G(p) * pa;
				b += GFX_B(p) * pa;
			}
			if (!a)
				continue;
			gfx_plot(fb, x + ix, y + iy, rgb(r / a, g / a, b / a),
				 (a / 9) * (dim ? 130 : 255) / 255);
		}
	}
}

void bevel(struct gfx_canvas *fb, int x, int y, int w, int h, bool pressed)
{
	gfx_rrect(fb, x, y, w, h, 4, EDGE, 255);
	gfx_rrect(fb, x + 1, y + 1, w - 2, h - 2, 3, pressed ? SHADE : PLATINUM, 255);
	if (pressed) {
		gfx_fill(fb, x + 2, y + 1, w - 4, 2, rgb(120, 120, 126));
		gfx_fill(fb, x + 2, y + 3, w - 4, 1, rgb(142, 142, 148));
	} else {
		gfx_fill(fb, x + 3, y + 1, w - 6, 1, LIGHT);
		gfx_fill(fb, x + 1, y + 3, 1, h - 6, LIGHT);
		gfx_fill(fb, x + 3, y + h - 2, w - 6, 1, SHADE);
		gfx_fill(fb, x + w - 2, y + 3, 1, h - 6, SHADE);
	}
}

/* the hix mark: a rounded blue tile with a white lower-case h */
void logo(struct gfx_canvas *fb, int x, int y)
{
	gfx_rrect(fb, x, y, 18, 18, 4, ACCENT_DARK, 255);
	gfx_rrect(fb, x + 1, y + 1, 16, 16, 3, ACCENT, 255);
	gfx_fill(fb, x + 5, y + 3, 2, 12, LIGHT);
	gfx_fill(fb, x + 7, y + 7, 4, 2, LIGHT);
	gfx_fill(fb, x + 11, y + 7, 2, 8, LIGHT);
}

/* the dock keeps its buttons between the menus and the clock */
int tasks_left(const struct steel_panel *p)
{
	int i, x = 36;

	for (i = 0; i < p->nmenus; i++)
		x = max(x, p->menu[i].x + p->menu[i].w);
	return x + 14;
}

int tasks_right(const struct steel_panel *p, int fbw)
{
	int cw = p->clock[0] ? gfx_text_width(&gfx_font_ubs, p->clock) : 0;

	return fbw - cw - 40;
}

int task_width(const struct steel_panel *p, int fbw)
{
	int room = tasks_right(p, fbw) - tasks_left(p);
	int w = p->ntasks ? (room + TASK_GAP) / p->ntasks - TASK_GAP : TASK_MAX_W;

	return max(TASK_MIN_W, min(w, TASK_MAX_W));
}

int task_x(const struct steel_panel *p, int fbw, int i)
{
	return tasks_left(p) + i * (task_width(p, fbw) + TASK_GAP);
}

void fit_text(char *out, size_t cap, const char *s, int px)
{
	size_t n = strlen(s);

	if (n >= cap)
		n = cap - 1;
	memcpy(out, s, n);
	out[n] = 0;
	while (n > 1 && gfx_text_width(&gfx_font_ubs, out) > px) {
		out[--n] = 0;
		if (n > 2) {
			out[n - 1] = '.';
			out[n - 2] = '.';
		}
	}
}

void draw_top(struct gfx_canvas *fb, const struct steel_panel *p)
{
	int i, w = fb->w;
	int ty = (PANEL_TOP_H - 16) / 2;

	gfx_vgrad(fb, 0, 0, w, PANEL_TOP_H - 1, TOP_A, TOP_B);
	gfx_fill(fb, 0, PANEL_TOP_H - 1, w, 1, EDGE);
	gfx_fill(fb, 0, 0, w, 1, LIGHT);
	logo(fb, 8, 3);

	for (i = 0; i < p->nmenus; i++) {
		const struct panel_menu *m = &p->menu[i];
		bool open = p->open == i;

		if (open) {
			gfx_fill(fb, m->x, 1, m->w, PANEL_TOP_H - 2, ACCENT);
			gfx_text(fb, &gfx_font_ubs, m->x + 8, ty + 1, m->label, LIGHT);
		} else {
			gfx_text(fb, &gfx_font_ubs, m->x + 8, ty + 1, m->label, INK);
		}
	}
	for (i = 0; i < p->ntasks; i++) {
		const struct panel_task *t = &p->task[i];
		bool on = t->flags & PT_ACTIVE;
		bool dim = t->flags & PT_MIN;
		int x = task_x(p, w, i), tw = task_width(p, w), y = (PANEL_TOP_H - TASK_H) / 2;
		char buf[24];

		if (x + tw > tasks_right(p, w) + TASK_GAP + 1)
			break;
		bevel(fb, x, y, tw, TASK_H, on);
		small_icon(fb, x + 4, y + 2 + (on ? 1 : 0), t->icon, dim);
		fit_text(buf, sizeof(buf), t->title, tw - 28);
		gfx_text(fb, &gfx_font_ubs, x + 24, y + 2 + (on ? 1 : 0), buf,
			 dim ? INK_OFF : INK);
	}
	if (p->clock[0]) {
		int cw = gfx_text_width(&gfx_font_ubs, p->clock);

		gfx_fill(fb, w - cw - 26, 4, 1, PANEL_TOP_H - 9, SHADE);
		gfx_text(fb, &gfx_font_ubs, w - cw - 12, ty + 1, p->clock, INK);
	}
}

} // namespace

extern "C" {

int panel_menu_width(const char *label)
{
	return gfx_text_width(&gfx_font_ubs, label) + 16;
}

void panel_draw(struct gfx_canvas *fb, const struct steel_panel *p)
{
	gfx_clip_reset(fb);
	draw_top(fb, p);
}

bool panel_task_rect(int screen_w, const struct steel_panel *p, int i, int *x,
		     int *w)
{
	if (i < 0 || i >= p->ntasks)
		return false;
	*x = task_x(p, screen_w, i);
	*w = task_width(p, screen_w);
	return *x + *w <= tasks_right(p, screen_w) + TASK_GAP + 1;
}

int panel_hit(int screen_w, const struct steel_panel *p, int x, int y)
{
	int i, tx, tw;

	if (y < 0 || y >= PANEL_TOP_H)
		return PANEL_HIT_NONE;
	for (i = 0; i < p->nmenus; i++)
		if (x >= p->menu[i].x && x < p->menu[i].x + p->menu[i].w)
			return PANEL_HIT_MENU + i;
	for (i = 0; i < p->ntasks; i++)
		if (panel_task_rect(screen_w, p, i, &tx, &tw) && x >= tx && x < tx + tw)
			return PANEL_HIT_TASK + i;
	return PANEL_HIT_BAR;
}

} // extern "C"
