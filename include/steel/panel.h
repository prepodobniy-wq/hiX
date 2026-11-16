/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * steel panel: the two bars around the desktop grid.
 *
 * The cell grid of the pixel display is 960x700 and sits in a 1024x768
 * screen, below a strip of 24 pixels. That strip is the panel (the dock): the
 * global menus on the left, one button per window in the middle, the clock on
 * the right. Below the grid the desktop shows through. The panel is not made
 * of cells.
 * The desktop describes what it wants in a struct steel_panel, steel draws
 * it (steel/panel.cpp, C++) and answers hit tests for the mouse.
 */
#ifndef _STEEL_PANEL_H
#define _STEEL_PANEL_H

#include <hix/types.h>
#include <hix/linkage.h>
#include <steel/gfx.h>

#define PANEL_TOP_H	24
#define PANEL_MENUS	3
#define PANEL_TASKS	12

#define PT_ACTIVE	1	/* the window with the focus: drawn pressed */
#define PT_MIN		2	/* minimised: drawn dimmed */

struct panel_menu {
	char label[12];
	s16 x, w;		/* pixels from the left of the screen */
};

struct panel_task {
	char title[24];
	u8 icon;		/* theme icon id */
	u8 flags;
};

struct steel_panel {
	struct panel_menu menu[PANEL_MENUS];
	int nmenus;
	int open;		/* index of the open menu, -1 = none */
	char clock[8];		/* "21:07" */
	struct panel_task task[PANEL_TASKS];
	int ntasks;
};

#define PANEL_HIT_NONE	(-1)	/* not on the panel */
#define PANEL_HIT_BAR	(-2)	/* on a panel, on nothing in particular */
#define PANEL_HIT_MENU	0	/* + menu index */
#define PANEL_HIT_TASK	100	/* + task index */

HIX_BEGIN_C

void panel_draw(struct gfx_canvas *fb, const struct steel_panel *p);
/* what is at screen pixel (x, y)? @screen_w is the width of the screen */
int panel_hit(int screen_w, const struct steel_panel *p, int x, int y);
/* where button @i of the dock is; false if there is no such button */
bool panel_task_rect(int screen_w, const struct steel_panel *p, int i, int *x,
		     int *w);
/* pixel width of a menu title, padding included */
int panel_menu_width(const char *label);

HIX_END_C

#endif /* _STEEL_PANEL_H */
