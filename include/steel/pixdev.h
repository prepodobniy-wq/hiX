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
#include <steel/panel.h>

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

#define PIX_MAX_H	1200		/* rows the dirty-span table can hold */
#define PIX_FLOAT_SHADOW 6		/* soft shadow around a floating window */
#define PIX_SPRITE_PX	(PIX_COLS * PIX_CELL_W * PIX_ROWS * PIX_CELL_H)

struct pixdev {
	/*
	 * Everything is drawn into @fb, a clean copy of the desktop in plain
	 * memory. Reading and alpha-blending in video memory is very slow, so
	 * the real screen only ever gets whole spans copied to it (pix_flush),
	 * with the floating window and the pointer laid over the copy.
	 */
	struct gfx_canvas fb;
	struct gfx_canvas screen;	/* the real framebuffer */
	s16 span0[PIX_MAX_H], span1[PIX_MAX_H];	/* dirty x range per row */
	int off_x, off_y;		/* pixel position of cell (0, 0) */
	/* a window being dragged: a pixel sprite at an exact position */
	bool fl_on, fl_redo;
	int fl_x, fl_y, fl_w, fl_h;	/* pixels on screen */
	int fl_cx, fl_cy, fl_cw, fl_ch;	/* the cells it was drawn from */
	u32 *sprite;
	steel_cell_t fl_cells[PIX_COLS * PIX_ROWS];
	steel_cell_t prev[PIX_COLS * PIX_ROWS];
	bool have_prev;
	struct pix_client client[PIX_CLIENTS];
	struct steel_panel panel;
	bool have_panel;
	int cur_x, cur_y;
	bool cur_visible;
};

/*
 * @fb is the whole screen; the cell grid is centred in it, below the top bar.
 * @back is memory for a copy of it (fb->w * fb->h pixels), @sprite for the
 * floating window (PIX_SPRITE_PX pixels).
 */
void pixdev_init(struct pixdev *d, const struct gfx_canvas *fb, u32 *back,
		 u32 *sprite);
void pixdev_present(struct pixdev *d, const steel_cell_t *cells);
/* draw the panels if they differ from what is on screen */
void pixdev_panel(struct pixdev *d, const struct steel_panel *p);
void pixdev_cursor(struct pixdev *d, int x, int y, bool visible);
/* geometry for input: where the grid sits and how big a cell is */
static inline int pixdev_off_x(const struct gfx_canvas *fb)
{
	return (fb->w - PIX_COLS * PIX_CELL_W) / 2;
}
static inline int pixdev_off_y(const struct gfx_canvas *fb)
{
	/* the top bar is 24 pixels; what is left below the grid is the taskbar */
	return PANEL_TOP_H;
}

/*
 * Show one window at an exact pixel position instead of its cell position:
 * the smooth way to drag. @cells is the screen with only that window drawn;
 * the rectangle (cx, cy, cw, ch) of it becomes the sprite, which is shown at
 * (px, py). The cells given to pixdev_present() must not contain the window.
 */
int pixdev_float(struct pixdev *d, const steel_cell_t *cells, int cx, int cy,
		 int cw, int ch, int px, int py);
void pixdev_float_off(struct pixdev *d);

int pixdev_client_set(struct pixdev *d, int idx, int x, int y, int w, int h,
		      const u32 *px, int stride);
void pixdev_client_clear(struct pixdev *d, int idx);
/* the client changed a rectangle (client pixels) */
void pixdev_client_dirty(struct pixdev *d, int idx, int x, int y, int w, int h);

/* the 16 colours of the cell attribute */
u32 pixdev_color(int idx);

#endif /* _STEEL_PIXDEV_H */
