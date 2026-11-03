/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * steel: the hix graphics layer (the hix graphics layer).
 *
 * The model, bottom to top:
 *
 *   steel_device    a display backend (VGA text today, a framebuffer later)
 *   steel_surface   a grid of cells you can draw into
 *   steel_cmdbuf    drawing commands recorded first, executed on commit
 *   steel_layer     a surface placed in a window; the compositor stacks
 *                   layers into a final surface and presents it
 *   steel_console   the text terminal printk and the tty write to
 *
 * A cell is 16 bit: character in the low byte, colour attribute in the high
 * byte (VGA text mode layout, code page 437 glyphs).
 */
#ifndef _STEEL_STEEL_H
#define _STEEL_STEEL_H

#include <hix/types.h>
#include <hix/list.h>

typedef u16 steel_cell_t;

enum steel_color {
	STEEL_BLACK, STEEL_BLUE, STEEL_GREEN, STEEL_CYAN,
	STEEL_RED, STEEL_MAGENTA, STEEL_BROWN, STEEL_LGRAY,
	STEEL_DGRAY, STEEL_LBLUE, STEEL_LGREEN, STEEL_LCYAN,
	STEEL_LRED, STEEL_LMAGENTA, STEEL_YELLOW, STEEL_WHITE,
};

#define STEEL_ATTR(fg, bg)	((u8)(((bg) << 4) | (fg)))
#define STEEL_CELL(ch, attr)	((steel_cell_t)(((attr) << 8) | (u8)(ch)))
#define STEEL_CH(cell)		((u8)((cell) & 0xFF))
#define STEEL_AT(cell)		((u8)((cell) >> 8))

/* code page 437 pieces used for drawing */
#define STEEL_SHADE_LIGHT	0xB0
#define STEEL_SHADE_MEDIUM	0xB1
#define STEEL_SHADE_DARK	0xB2
#define STEEL_BLOCK_FULL	0xDB

enum steel_box_style {
	STEEL_BOX_SINGLE,
	STEEL_BOX_DOUBLE,
};

/* ---- surfaces -------------------------------------------------------- */

struct steel_surface {
	int w, h;
	steel_cell_t *cells;
	bool owns_cells;
};

/* wrap caller-provided storage of w*h cells */
void steel_surface_init(struct steel_surface *s, int w, int h,
			steel_cell_t *storage);
struct steel_surface *steel_surface_create(int w, int h);
void steel_surface_destroy(struct steel_surface *s);

void steel_fill(struct steel_surface *s, u8 ch, u8 attr);
void steel_put(struct steel_surface *s, int x, int y, u8 ch, u8 attr);
steel_cell_t steel_get(const struct steel_surface *s, int x, int y);
int steel_puts(struct steel_surface *s, int x, int y, const char *str,
	       u8 attr);
void steel_rect(struct steel_surface *s, int x, int y, int w, int h, u8 ch,
		u8 attr);
void steel_hline(struct steel_surface *s, int x, int y, int len, u8 ch,
		 u8 attr);
void steel_box(struct steel_surface *s, int x, int y, int w, int h,
	       enum steel_box_style style, u8 attr);
void steel_blit(struct steel_surface *dst, int dx, int dy,
		const struct steel_surface *src, int sx, int sy, int w, int h);
/* recolour a rectangle without touching the characters */
void steel_tint(struct steel_surface *s, int x, int y, int w, int h, u8 attr);

/* ---- devices --------------------------------------------------------- */

struct steel_device {
	const char *name;
	int w, h;
	void (*present)(struct steel_device *dev,
			const struct steel_surface *s);
	void (*set_cursor)(struct steel_device *dev, int x, int y,
			   bool visible);
};

void steel_set_device(struct steel_device *dev);
struct steel_device *steel_get_device(void);
/* send a surface to the display; no-op without a device */
void steel_present(const struct steel_surface *s);
void steel_set_cursor(int x, int y, bool visible);

/* ---- command buffers ------------------------------------------------- */

#define STEEL_CMDBUF_MAX	128

enum steel_op {
	STEEL_OP_CLEAR,
	STEEL_OP_FILL_RECT,
	STEEL_OP_BOX,
	STEEL_OP_TEXT,
	STEEL_OP_HLINE,
	STEEL_OP_TINT,
};

struct steel_cmd {
	u8 op, ch, attr, style;
	s16 x, y, w, h;
	const char *text;		/* must outlive the commit */
};

struct steel_cmdbuf {
	struct steel_surface *target;
	struct steel_cmd cmds[STEEL_CMDBUF_MAX];
	int count;
};

void steel_cmdbuf_begin(struct steel_cmdbuf *cb, struct steel_surface *target);
int steel_enc_clear(struct steel_cmdbuf *cb, u8 ch, u8 attr);
int steel_enc_fill_rect(struct steel_cmdbuf *cb, int x, int y, int w, int h,
			u8 ch, u8 attr);
int steel_enc_box(struct steel_cmdbuf *cb, int x, int y, int w, int h,
		  enum steel_box_style style, u8 attr);
int steel_enc_text(struct steel_cmdbuf *cb, int x, int y, const char *text,
		   u8 attr);
int steel_enc_hline(struct steel_cmdbuf *cb, int x, int y, int len, u8 ch,
		    u8 attr);
int steel_enc_tint(struct steel_cmdbuf *cb, int x, int y, int w, int h,
		   u8 attr);
/* run every command, then present the target; returns the command count */
int steel_commit(struct steel_cmdbuf *cb);
/* run every command without presenting (used by tests and compositing) */
int steel_execute(struct steel_cmdbuf *cb);

/* ---- compositor ------------------------------------------------------ */

struct steel_layer {
	int x, y;			/* top-left of the window frame */
	char title[24];
	u8 frame_attr;
	struct steel_surface *content;	/* the area inside the frame */
	struct list_head node;		/* z-order: last = topmost */
};

struct steel_layer *steel_layer_create(int x, int y, int content_w,
				       int content_h, const char *title);
void steel_layer_destroy(struct steel_layer *layer);
void steel_layer_raise(struct steel_layer *layer);
void steel_layer_move(struct steel_layer *layer, int x, int y);

/* desktop background + all layers into @dest (does not present) */
void steel_compose(struct steel_surface *dest, const char *menubar_text);

/* ---- console --------------------------------------------------------- */

void steel_con_init(void);
void steel_con_register_printk(void);	/* printk -> screen */
void steel_con_write(const char *s, size_t n);
void steel_con_set_attr(u8 attr);
void steel_con_clear(void);
/* scrollback: positive = towards older lines, 0 snaps back to live */
void steel_con_scroll_view(int delta);
void steel_con_redraw(void);
/* a program that draws the whole screen itself holds the console off */
void steel_con_set_suspended(bool on);
struct steel_surface *steel_con_surface(void);

#endif /* _STEEL_STEEL_H */
