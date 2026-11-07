/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * steel gfx: the pixel half of steel.
 *
 * A gfx_canvas is a rectangle of 32 bit pixels (0x00RRGGBB) with a clip
 * rectangle. Everything here is integer arithmetic: the kernel is built
 * without an FPU, and anti-aliasing works on distances in 1/16 pixel.
 *
 * The same code draws the desktop (steel/pixdev.c) and the programs that
 * bring their own pixels (chess); the host tests run it too and write PNGs.
 */
#ifndef _STEEL_GFX_H
#define _STEEL_GFX_H

#include <hix/types.h>

#define GFX_RGB(r, g, b)	(((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))
#define GFX_R(c)		(((c) >> 16) & 0xFF)
#define GFX_G(c)		(((c) >> 8) & 0xFF)
#define GFX_B(c)		((c) & 0xFF)

struct gfx_canvas {
	int w, h;
	int stride;			/* pixels per row */
	u32 *px;
	int clip_x0, clip_y0, clip_x1, clip_y1;	/* [x0,x1) x [y0,y1) */
};

/* a bitmap font: @count glyphs from @first, each w*h bytes of coverage */
struct gfx_font {
	int w, h;			/* glyph cell (maximum width for ui fonts) */
	int first, count;
	const u8 *alpha;
	const u8 *adv;			/* advance per glyph, NULL = fixed w */
	int base;			/* baseline row inside the cell */
};

extern const struct gfx_font gfx_font_cell;	/* 12x28, code page 437 */
extern const struct gfx_font gfx_font_ui;	/* proportional, ~14 px */
extern const struct gfx_font gfx_font_uib;	/* proportional bold, ~20 px */

#define GFX_SPRITE	64		/* chess sprite size */
extern const u8 gfx_chess_sprites[6 * GFX_SPRITE * GFX_SPRITE];

void gfx_canvas_init(struct gfx_canvas *c, int w, int h, u32 *px);
void gfx_clip_reset(struct gfx_canvas *c);
void gfx_clip_set(struct gfx_canvas *c, int x, int y, int w, int h);

static inline u32 gfx_mix(u32 dst, u32 src, unsigned int a)	/* a: 0..255 */
{
	unsigned int ia = 255 - a;
	unsigned int r = (GFX_R(dst) * ia + GFX_R(src) * a + 127) / 255;
	unsigned int g = (GFX_G(dst) * ia + GFX_G(src) * a + 127) / 255;
	unsigned int b = (GFX_B(dst) * ia + GFX_B(src) * a + 127) / 255;

	return GFX_RGB(r, g, b);
}

u32 gfx_isqrt(u32 v);
void gfx_plot(struct gfx_canvas *c, int x, int y, u32 color, unsigned int a);

void gfx_fill(struct gfx_canvas *c, int x, int y, int w, int h, u32 color);
void gfx_fill_a(struct gfx_canvas *c, int x, int y, int w, int h, u32 color,
		unsigned int alpha);
void gfx_vgrad(struct gfx_canvas *c, int x, int y, int w, int h, u32 top,
	       u32 bottom);
/* 1 px frame inside the rectangle */
void gfx_frame(struct gfx_canvas *c, int x, int y, int w, int h, u32 color);
/* rounded rectangle with anti-aliased corners of radius @r */
void gfx_rrect(struct gfx_canvas *c, int x, int y, int w, int h, int r,
	       u32 color, unsigned int alpha);
/* disc and ring; centre and radii in 1/16 pixel */
void gfx_disc16(struct gfx_canvas *c, int cx16, int cy16, int r16, u32 color,
		unsigned int alpha);
void gfx_ring16(struct gfx_canvas *c, int cx16, int cy16, int r16,
		int thick16, u32 color, unsigned int alpha);
/* a thick line with round ends; coordinates and width in 1/16 pixel */
void gfx_line16(struct gfx_canvas *c, int x0, int y0, int x1, int y1,
		int width16, u32 color, unsigned int alpha);
/* blend @color through an 8 bit coverage mask; @alpha scales it (255 = as is) */
void gfx_mask(struct gfx_canvas *c, int x, int y, const u8 *mask, int mw,
	      int mh, int mstride, u32 color, unsigned int alpha);
/* draw a w*h bitmap of straight-alpha pixels (0xAARRGGBB) */
void gfx_blit_argb(struct gfx_canvas *c, int x, int y, const u32 *src, int w,
		   int h);
/* copy a rectangle between canvases (no blending) */
void gfx_blit(struct gfx_canvas *dst, int dx, int dy,
	      const struct gfx_canvas *src, int sx, int sy, int w, int h);

int gfx_text(struct gfx_canvas *c, const struct gfx_font *f, int x, int y,
	     const char *s, u32 color);
int gfx_text_width(const struct gfx_font *f, const char *s);
/* centred in [x, x+w) */
void gfx_text_center(struct gfx_canvas *c, const struct gfx_font *f, int x,
		     int y, int w, const char *s, u32 color);

#endif /* _STEEL_GFX_H */
