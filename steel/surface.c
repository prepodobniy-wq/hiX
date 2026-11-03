// SPDX-License-Identifier: GPL-2.0-only
/* steel: surfaces and drawing primitives. All drawing is clipped. */
#include <hix/kernel.h>
#include <aether/kalloc.h>
#include <steel/steel.h>

void steel_surface_init(struct steel_surface *s, int w, int h,
			steel_cell_t *storage)
{
	s->w = w;
	s->h = h;
	s->cells = storage;
	s->owns_cells = false;
}

struct steel_surface *steel_surface_create(int w, int h)
{
	struct steel_surface *s;

	if (w <= 0 || h <= 0)
		return NULL;
	s = kzalloc(sizeof(*s));
	if (!s)
		return NULL;
	s->cells = kzalloc((size_t)w * h * sizeof(steel_cell_t));
	if (!s->cells) {
		kfree(s);
		return NULL;
	}
	s->w = w;
	s->h = h;
	s->owns_cells = true;
	return s;
}

void steel_surface_destroy(struct steel_surface *s)
{
	if (!s)
		return;
	if (s->owns_cells)
		kfree(s->cells);
	kfree(s);
}

static inline bool inside(const struct steel_surface *s, int x, int y)
{
	return x >= 0 && y >= 0 && x < s->w && y < s->h;
}

void steel_fill(struct steel_surface *s, u8 ch, u8 attr)
{
	int i, n = s->w * s->h;

	for (i = 0; i < n; i++)
		s->cells[i] = STEEL_CELL(ch, attr);
}

void steel_put(struct steel_surface *s, int x, int y, u8 ch, u8 attr)
{
	if (inside(s, x, y))
		s->cells[y * s->w + x] = STEEL_CELL(ch, attr);
}

steel_cell_t steel_get(const struct steel_surface *s, int x, int y)
{
	return inside(s, x, y) ? s->cells[y * s->w + x] : 0;
}

/* returns the number of characters that were drawn */
int steel_puts(struct steel_surface *s, int x, int y, const char *str,
	       u8 attr)
{
	int n = 0;

	for (; *str; str++, x++) {
		if (inside(s, x, y)) {
			steel_put(s, x, y, (u8)*str, attr);
			n++;
		}
	}
	return n;
}

void steel_rect(struct steel_surface *s, int x, int y, int w, int h, u8 ch,
		u8 attr)
{
	int ix, iy;

	for (iy = y; iy < y + h; iy++)
		for (ix = x; ix < x + w; ix++)
			steel_put(s, ix, iy, ch, attr);
}

void steel_hline(struct steel_surface *s, int x, int y, int len, u8 ch,
		 u8 attr)
{
	steel_rect(s, x, y, len, 1, ch, attr);
}

void steel_box(struct steel_surface *s, int x, int y, int w, int h,
	       enum steel_box_style style, u8 attr)
{
	static const u8 glyphs[2][6] = {
		/* tl     tr     bl     br     horiz  vert */
		{ 0xDA, 0xBF, 0xC0, 0xD9, 0xC4, 0xB3 },
		{ 0xC9, 0xBB, 0xC8, 0xBC, 0xCD, 0xBA },
	};
	const u8 *g = glyphs[style == STEEL_BOX_DOUBLE];
	int i;

	if (w < 2 || h < 2)
		return;
	for (i = 1; i < w - 1; i++) {
		steel_put(s, x + i, y, g[4], attr);
		steel_put(s, x + i, y + h - 1, g[4], attr);
	}
	for (i = 1; i < h - 1; i++) {
		steel_put(s, x, y + i, g[5], attr);
		steel_put(s, x + w - 1, y + i, g[5], attr);
	}
	steel_put(s, x, y, g[0], attr);
	steel_put(s, x + w - 1, y, g[1], attr);
	steel_put(s, x, y + h - 1, g[2], attr);
	steel_put(s, x + w - 1, y + h - 1, g[3], attr);
}

void steel_blit(struct steel_surface *dst, int dx, int dy,
		const struct steel_surface *src, int sx, int sy, int w, int h)
{
	int ix, iy;

	for (iy = 0; iy < h; iy++) {
		for (ix = 0; ix < w; ix++) {
			if (!inside(src, sx + ix, sy + iy) ||
			    !inside(dst, dx + ix, dy + iy))
				continue;
			dst->cells[(dy + iy) * dst->w + dx + ix] =
				src->cells[(sy + iy) * src->w + sx + ix];
		}
	}
}

void steel_tint(struct steel_surface *s, int x, int y, int w, int h, u8 attr)
{
	int ix, iy;

	for (iy = y; iy < y + h; iy++) {
		for (ix = x; ix < x + w; ix++) {
			if (inside(s, ix, iy)) {
				steel_cell_t *c = &s->cells[iy * s->w + ix];

				*c = STEEL_CELL(STEEL_CH(*c), attr);
			}
		}
	}
}
