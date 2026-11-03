/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * steel pixdev: turns the 80x25 cell surface of the desktop into pixels.
 *
 * Programs keep talking in cells (steel_cell_t, code page 437). This layer
 * draws each cell with a smooth 12x28 font, replaces the line and block
 * characters by exact shapes, keeps a mouse pointer with pixel precision and
 * redraws only the cells that changed.
 *
 * A window that wants real pixels (chess) fills its client area with "hole"
 * cells; steel then shows the registered pixel client there instead of text.
 * Because holes are ordinary cells, windows in front of a pixel client
 * cover it correctly with no extra bookkeeping.
 */
#ifndef _STEEL_PIXDEV_H
#define _STEEL_PIXDEV_H

#include <steel/steel.h>
#include <steel/gfx.h>

#define PIX_COLS	80
#define PIX_ROWS	25
#define PIX_CELL_W	12
#define PIX_CELL_H	28
#define PIX_CLIENTS	4
#define PIX_CUR_W	12
#define PIX_CUR_H	19

/* the cell that stands for pixel client @i */
#define STEEL_HOLE(i)		STEEL_CELL(0, 0xA0 + (i))
#define STEEL_IS_HOLE(cell)	(STEEL_CH(cell) == 0 && STEEL_AT(cell) >= 0xA0 && \
				 STEEL_AT(cell) < 0xA0 + PIX_CLIENTS)

struct pix_client {
	bool used;
	int x, y, w, h;			/* where it sits on screen, pixels */
	const u32 *px;
	int stride;
	int dx0, dy0, dx1, dy1;		/* dirty rectangle, client pixels */
};

struct pixdev {
	struct gfx_canvas fb;
	int off_x, off_y;		/* pixel position of cell (0, 0) */
	steel_cell_t prev[PIX_COLS * PIX_ROWS];
	bool have_prev;
	struct pix_client client[PIX_CLIENTS];
	int cur_x, cur_y;
	bool cur_visible, under_valid;
	int under_x, under_y;
	u32 under[PIX_CUR_W * PIX_CUR_H];
};

/* @fb is the whole screen; the cell grid is centred in it */
void pixdev_init(struct pixdev *d, const struct gfx_canvas *fb);
void pixdev_present(struct pixdev *d, const steel_cell_t *cells);
void pixdev_cursor(struct pixdev *d, int x, int y, bool visible);
/* geometry for input: where the grid sits and how big a cell is */
static inline int pixdev_off_x(const struct gfx_canvas *fb)
{
	return (fb->w - PIX_COLS * PIX_CELL_W) / 2;
}
static inline int pixdev_off_y(const struct gfx_canvas *fb)
{
	return (fb->h - PIX_ROWS * PIX_CELL_H) / 2;
}

int pixdev_client_set(struct pixdev *d, int idx, int x, int y, int w, int h,
		      const u32 *px, int stride);
void pixdev_client_clear(struct pixdev *d, int idx);
/* the client changed a rectangle (client pixels) */
void pixdev_client_dirty(struct pixdev *d, int idx, int x, int y, int w, int h);

/* the 16 colours of the cell attribute */
u32 pixdev_color(int idx);

#endif /* _STEEL_PIXDEV_H */
