/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * steel theme: the Argent look of the pixel desktop.
 *
 * The desktop still speaks in cells. A cell may carry a role (see
 * steel/steel.h): "I am a piece of a window title", "I am part of a button".
 * The pixel display hands such cells to the theme (steel/theme.cpp, C++),
 * which draws bevels, pinstripes, rounded buttons, shadows and icons instead
 * of a character. A text display ignores roles and shows the character.
 *
 * The role byte of a cell is a role number in its low 6 bits plus two shadow
 * flags, so a shadow can fall over any other cell. The aux byte means
 * something different for each role, documented next to it below.
 */
#ifndef _STEEL_THEME_H
#define _STEEL_THEME_H

#include <hix/types.h>
#include <hix/linkage.h>
#include <steel/steel.h>
#include <steel/gfx.h>

enum theme_role {
	TR_NONE,
	TR_TITLE,	/* window title bar, one cell of it */
	TR_FRAME,	/* window or menu border */
	TR_MENUBAR,	/* the menu titles of a window */
	TR_BUTTON,	/* push button; the label cells are its characters */
	TR_DROP,	/* an item of an open menu */
	TR_ICON,	/* one cell of an icon picture */
	TR_LABEL,	/* an icon label, centred over its cells */
};

#define TR_ID(role)		((role) & 0x3F)
#define TR_SHADOW_R		0x40	/* a window's shadow falls on this cell, right side */
#define TR_SHADOW_B		0x80	/* ... below it */

/* TR_TITLE aux: part in bits 0-2 */
#define TP_BAR			0
#define TP_TEXT			1	/* ch is a character of the title */
#define TP_CLOSE		2
#define TP_ZOOM			3
#define TP_COLLAPSE		4
#define TA_PART(aux)		((aux) & 7)
#define TA_ACTIVE		0x10
#define TA_FIRST		0x20	/* leftmost cell of the bar */
#define TA_LAST			0x40	/* rightmost cell of the bar */

/* TR_FRAME aux: which sides of the cell are the outside of the frame */
#define FS_L			1
#define FS_R			2
#define FS_T			4
#define FS_B			8
#define FA_ACTIVE		0x10
#define FA_FLAT			0x20	/* a menu border: thin line, no bevel */

/* TR_MENUBAR aux */
#define MB_SELECTED		1

/* TR_BUTTON aux: part in bits 0-1; ch of the body cells is the label */
#define BP_LEFT			0
#define BP_BODY			1
#define BP_RIGHT		2
#define BA_PART(aux)		((aux) & 3)
#define BA_DEFAULT		0x10
#define BA_ON_GRAY		0x20	/* sits on a dialog's gray, else on white */

/* TR_DROP aux */
#define DP_ITEM			0
#define DP_SELECTED		1
#define DP_SEPARATOR		2
#define DP_DISABLED		3

/* TR_ICON: ch = (column << 4) | row inside the 6x2 cell picture (| TI_DESKTOP
 * when it lies on the wallpaper), aux = icon id */
#define TI_DESKTOP		0x80
#define TI_CELLS_W		6	/* two outer cells stay empty */
#define TI_CELLS_H		2

/* TR_LABEL aux: flags in bits 0-1, the cell's index within the label in bits
 * 2-5. A label is TL_SPAN cells wide; a window may cover part of it, so the
 * theme remembers the whole text while it is visible. */
#define TL_SELECTED		1
#define TL_ON_DESKTOP		2
#define TL_INDEX(k)		((k) << 2)
#define TL_GET_INDEX(aux)	(((aux) >> 2) & 15)
#define TL_SPAN			12

/* cells of the desktop itself have this attribute and no role */
#define THEME_DESKTOP_ATTR	STEEL_ATTR(STEEL_CYAN, STEEL_CYAN)

HIX_BEGIN_C

/* the cell is bare desktop */
bool theme_is_wallpaper(steel_cell_t cell);
/* non-zero when the look of the cell depends on its neighbours (a text run):
 * all cells of one run have the same key */
unsigned int theme_run_key(steel_cell_t cell);
void theme_wallpaper(struct gfx_canvas *fb, int x, int y, int w, int h);
/* draw the cell (cx, cy) of the grid whose top left pixel is (px, py) */
void theme_draw_cell(struct gfx_canvas *fb, int px, int py, int cx, int cy,
		     const steel_cell_t *cells, int cols, int rows);
/* darken the part of the cell a window's shadow covers */
void theme_shadow(struct gfx_canvas *fb, int px, int py, unsigned int bits);
/* icon number for a program name, 0 = the generic icon */
int theme_icon_id(const char *name);

HIX_END_C

#endif /* _STEEL_THEME_H */
