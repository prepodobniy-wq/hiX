// SPDX-License-Identifier: GPL-2.0-only
/* steel gfx: drawing into 32 bit canvases. Integer arithmetic only. */
#include <hix/kernel.h>
#include <steel/gfx.h>

void gfx_clip_reset(struct gfx_canvas *c)
{
	c->clip_x0 = c->clip_y0 = 0;
	c->clip_x1 = c->w;
	c->clip_y1 = c->h;
}

void gfx_canvas_init(struct gfx_canvas *c, int w, int h, u32 *px)
{
	c->w = w;
	c->h = h;
	c->stride = w;
	c->px = px;
	gfx_clip_reset(c);
}

void gfx_clip_set(struct gfx_canvas *c, int x, int y, int w, int h)
{
	c->clip_x0 = max(x, 0);
	c->clip_y0 = max(y, 0);
	c->clip_x1 = min(x + w, c->w);
	c->clip_y1 = min(y + h, c->h);
}

u32 gfx_isqrt(u32 v)
{
	u32 r = 0, bit = 1u << 30;

	while (bit > v)
		bit >>= 2;
	while (bit) {
		if (v >= r + bit) {
			v -= r + bit;
			r = (r >> 1) + bit;
		} else {
			r >>= 1;
		}
		bit >>= 2;
	}
	return r;
}

static inline bool in_clip(const struct gfx_canvas *c, int x, int y)
{
	return x >= c->clip_x0 && x < c->clip_x1 && y >= c->clip_y0 &&
	       y < c->clip_y1;
}

void gfx_plot(struct gfx_canvas *c, int x, int y, u32 color, unsigned int a)
{
	u32 *p;

	if (!a || !in_clip(c, x, y))
		return;
	p = &c->px[y * c->stride + x];
	*p = a >= 255 ? color : gfx_mix(*p, color, a);
}

/* intersect a rectangle with the clip; false when nothing is left */
static bool clip_rect(const struct gfx_canvas *c, int *x, int *y, int *w,
		      int *h)
{
	int x1 = *x + *w, y1 = *y + *h;

	*x = max(*x, c->clip_x0);
	*y = max(*y, c->clip_y0);
	x1 = min(x1, c->clip_x1);
	y1 = min(y1, c->clip_y1);
	*w = x1 - *x;
	*h = y1 - *y;
	return *w > 0 && *h > 0;
}

void gfx_fill(struct gfx_canvas *c, int x, int y, int w, int h, u32 color)
{
	int i, j;

	if (!clip_rect(c, &x, &y, &w, &h))
		return;
	for (j = 0; j < h; j++) {
		u32 *row = &c->px[(y + j) * c->stride + x];

		for (i = 0; i < w; i++)
			row[i] = color;
	}
}

void gfx_fill_a(struct gfx_canvas *c, int x, int y, int w, int h, u32 color,
		unsigned int alpha)
{
	int i, j;

	if (alpha >= 255) {
		gfx_fill(c, x, y, w, h, color);
		return;
	}
	if (!alpha || !clip_rect(c, &x, &y, &w, &h))
		return;
	for (j = 0; j < h; j++) {
		u32 *row = &c->px[(y + j) * c->stride + x];

		for (i = 0; i < w; i++)
			row[i] = gfx_mix(row[i], color, alpha);
	}
}

void gfx_vgrad(struct gfx_canvas *c, int x, int y, int w, int h, u32 top,
	       u32 bottom)
{
	int j;

	for (j = 0; j < h; j++) {
		unsigned int a = h > 1 ? (unsigned int)(j * 255 / (h - 1)) : 0;

		gfx_fill(c, x, y + j, w, 1, gfx_mix(top, bottom, a));
	}
}

void gfx_frame(struct gfx_canvas *c, int x, int y, int w, int h, u32 color)
{
	gfx_fill(c, x, y, w, 1, color);
	gfx_fill(c, x, y + h - 1, w, 1, color);
	gfx_fill(c, x, y, 1, h, color);
	gfx_fill(c, x + w - 1, y, 1, h, color);
}

/* coverage 0..16 of a pixel whose centre is @d16 from a circle edge at @r16 */
static inline unsigned int cover16(int r16, u32 d16)
{
	int v = r16 - (int)d16 + 8;

	return v < 0 ? 0 : v > 16 ? 16 : (unsigned int)v;
}

void gfx_rrect(struct gfx_canvas *c, int x, int y, int w, int h, int r,
	       u32 color, unsigned int alpha)
{
	int i, j;

	if (r * 2 > w)
		r = w / 2;
	if (r * 2 > h)
		r = h / 2;
	if (r <= 0) {
		gfx_fill_a(c, x, y, w, h, color, alpha);
		return;
	}
	gfx_fill_a(c, x + r, y, w - 2 * r, h, color, alpha);
	gfx_fill_a(c, x, y + r, r, h - 2 * r, color, alpha);
	gfx_fill_a(c, x + w - r, y + r, r, h - 2 * r, color, alpha);
	for (j = 0; j < r; j++) {
		for (i = 0; i < r; i++) {
			int dx = r * 16 - i * 16 - 8, dy = r * 16 - j * 16 - 8;
			unsigned int cv = cover16(r * 16,
				gfx_isqrt((u32)(dx * dx + dy * dy)));
			unsigned int a = cv * alpha / 16;

			gfx_plot(c, x + i, y + j, color, a);
			gfx_plot(c, x + w - 1 - i, y + j, color, a);
			gfx_plot(c, x + i, y + h - 1 - j, color, a);
			gfx_plot(c, x + w - 1 - i, y + h - 1 - j, color, a);
		}
	}
}

void gfx_ring16(struct gfx_canvas *c, int cx16, int cy16, int r16,
		int thick16, u32 color, unsigned int alpha)
{
	int x0 = (cx16 - r16) / 16 - 1, x1 = (cx16 + r16) / 16 + 2;
	int y0 = (cy16 - r16) / 16 - 1, y1 = (cy16 + r16) / 16 + 2;
	int mid = r16 - thick16 / 2, half = thick16 / 2;
	int x, y;

	for (y = y0; y < y1; y++) {
		for (x = x0; x < x1; x++) {
			int dx = x * 16 + 8 - cx16, dy = y * 16 + 8 - cy16;
			int d = (int)gfx_isqrt((u32)(dx * dx + dy * dy));
			int off = d > mid ? d - mid : mid - d;	/* from the ring's middle line */
			unsigned int cv = cover16(half, (u32)off);

			if (thick16 <= 0)
				return;
			gfx_plot(c, x, y, color, cv * alpha / 16);
		}
	}
}

void gfx_disc16(struct gfx_canvas *c, int cx16, int cy16, int r16, u32 color,
		unsigned int alpha)
{
	int x0 = (cx16 - r16) / 16 - 1, x1 = (cx16 + r16) / 16 + 2;
	int y0 = (cy16 - r16) / 16 - 1, y1 = (cy16 + r16) / 16 + 2;
	int x, y;

	for (y = y0; y < y1; y++) {
		for (x = x0; x < x1; x++) {
			int dx = x * 16 + 8 - cx16, dy = y * 16 + 8 - cy16;
			unsigned int cv = cover16(r16,
				gfx_isqrt((u32)(dx * dx + dy * dy)));

			gfx_plot(c, x, y, color, cv * alpha / 16);
		}
	}
}

void gfx_mask(struct gfx_canvas *c, int x, int y, const u8 *mask, int mw,
	      int mh, int mstride, u32 color, unsigned int alpha)
{
	int i, j;

	for (j = 0; j < mh; j++) {
		if (y + j < c->clip_y0 || y + j >= c->clip_y1)
			continue;
		for (i = 0; i < mw; i++) {
			unsigned int a = mask[j * mstride + i];

			if (a)
				gfx_plot(c, x + i, y + j, color,
					 (a * alpha + 127) / 255);
		}
	}
}

void gfx_blit_argb(struct gfx_canvas *c, int x, int y, const u32 *src, int w,
		   int h)
{
	int i, j;

	for (j = 0; j < h; j++) {
		if (y + j < c->clip_y0 || y + j >= c->clip_y1)
			continue;
		for (i = 0; i < w; i++) {
			u32 p = src[j * w + i];

			if (p >> 24)
				gfx_plot(c, x + i, y + j, p & 0xFFFFFF, p >> 24);
		}
	}
}

void gfx_blit(struct gfx_canvas *dst, int dx, int dy,
	      const struct gfx_canvas *src, int sx, int sy, int w, int h)
{
	int j;

	if (sx < 0) {
		dx -= sx;
		w += sx;
		sx = 0;
	}
	if (sy < 0) {
		dy -= sy;
		h += sy;
		sy = 0;
	}
	w = min(w, src->w - sx);
	h = min(h, src->h - sy);
	for (j = 0; j < h; j++) {
		int y = dy + j, x = dx, n = w, off = 0;

		if (y < dst->clip_y0 || y >= dst->clip_y1)
			continue;
		if (x < dst->clip_x0) {
			off = dst->clip_x0 - x;
			x = dst->clip_x0;
			n -= off;
		}
		if (x + n > dst->clip_x1)
			n = dst->clip_x1 - x;
		if (n > 0)
			memcpy(&dst->px[y * dst->stride + x],
			       &src->px[(sy + j) * src->stride + sx + off],
			       (size_t)n * sizeof(u32));
	}
}

static const u8 *glyph(const struct gfx_font *f, unsigned char ch, int *adv)
{
	int idx = ch - f->first;

	if (idx < 0 || idx >= f->count)
		idx = '?' - f->first;
	*adv = f->adv ? f->adv[idx] : f->w;
	return f->alpha + idx * f->w * f->h;
}

int gfx_text(struct gfx_canvas *c, const struct gfx_font *f, int x, int y,
	     const char *s, u32 color)
{
	int start = x;

	for (; *s; s++) {
		int adv;
		const u8 *g = glyph(f, (unsigned char)*s, &adv);

		gfx_mask(c, x, y, g, f->w, f->h, f->w, color, 255);
		x += adv;
	}
	return x - start;
}

int gfx_text_width(const struct gfx_font *f, const char *s)
{
	int w = 0, adv;

	for (; *s; s++) {
		glyph(f, (unsigned char)*s, &adv);
		w += adv;
	}
	return w;
}

void gfx_text_center(struct gfx_canvas *c, const struct gfx_font *f, int x,
		     int y, int w, const char *s, u32 color)
{
	gfx_text(c, f, x + (w - gfx_text_width(f, s)) / 2, y, s, color);
}
