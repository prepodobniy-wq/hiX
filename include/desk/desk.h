/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * desk: the hix desktop, a classic windowed window manager for the steel
 * text display, and the programs that run inside it.
 *
 * The desktop is an ordinary qubix process ("desk"). It owns the screen
 * through steelctl, reads keyboard and mouse events, keeps a stack of
 * windows and repaints everything after each event. Programs are small
 * event-driven objects (struct app); every window gets its own client
 * surface and some private memory, so the desktop needs no allocator.
 *
 *   desk_run()  the event loop, takes a backend so tests can drive it
 *   struct app  what a program provides
 *   struct win  one window
 */
#ifndef _DESK_DESK_H
#define _DESK_DESK_H

#include <hix/types.h>
#include <steel/steel.h>
#include <steel/gfx.h>
#include <qubix/input.h>

#define DESK_W		80
#define DESK_H		25
#define DESK_MAX_WINDOWS 12
#define WIN_PRIV_SIZE	8192

/* the look: the 16 colour hix desk scheme */
#define A_DESKTOP	STEEL_ATTR(STEEL_CYAN, STEEL_CYAN)
#define A_WIN		STEEL_ATTR(STEEL_BLACK, STEEL_WHITE)	/* client area */
#define A_GRAY		STEEL_ATTR(STEEL_BLACK, STEEL_LGRAY)	/* frames, dialogs */
#define A_TITLE		STEEL_ATTR(STEEL_WHITE, STEEL_BLUE)	/* active title */
#define A_TITLE_OFF	STEEL_ATTR(STEEL_BLACK, STEEL_WHITE)	/* inactive */
#define A_SEL		STEEL_ATTR(STEEL_WHITE, STEEL_BLUE)	/* highlight */
#define A_DIM		STEEL_ATTR(STEEL_DGRAY, STEEL_WHITE)

struct win;
struct app;

/* ---- menus ----------------------------------------------------------- */

struct menu_item {
	const char *label;
	int id;			/* passed to app->command; 0 = separator */
	const char *accel;	/* shown on the right, e.g. "Alt+F4" */
};

struct menu {
	const char *title;
	const struct menu_item *items;
	int nitems;
};

/* commands the desktop itself handles */
#define CMD_CLOSE	1001
#define CMD_ABOUT	1002
#define CMD_RESTORE	1003
#define CMD_MINIMIZE	1004
#define CMD_MAXIMIZE	1005
#define CMD_FIRST_WM	1000

/* the standard Help menu */
extern const struct menu_item desk_help_items[];
#define DESK_HELP_MENU	{ "Help", desk_help_items, 1 }

/* ---- programs -------------------------------------------------------- */

enum { MEV_DOWN, MEV_UP, MEV_MOVE, MEV_DBL };

#define APP_RESIZABLE	BIT(0)	/* may be maximized */
#define APP_SINGLE	BIT(1)	/* a second launch raises the first window */
#define APP_ACTION	BIT(2)	/* no window: just call ->action() */
#define APP_ROOT	BIT(3)	/* closing it ends the session */
#define APP_DIALOG	BIT(4)	/* modal, no minimize or maximize */

struct app {
	const char *name;	/* launch name: "notes" */
	const char *title;	/* window title and icon label */
	const char *icon[2];	/* two rows of six code page 437 characters */
	u8 icon_attr[2];
	int cw, ch;		/* client size in cells */
	unsigned int flags;
	unsigned int tick_ms;	/* 0: no ->tick() */
	const struct menu *menus;
	int nmenus;

	void (*open)(struct win *w, const char *arg);
	/* return false to veto closing (the app asks first, closes later) */
	bool (*close_request)(struct win *w);
	void (*close)(struct win *w);
	void (*paint)(struct win *w, struct steel_surface *c);
	/* true when the key was used; unused keys fall back to the desktop */
	bool (*key)(struct win *w, int key, u8 mods);
	/* x, y are client coordinates and may lie outside while dragging */
	void (*mouse)(struct win *w, int ev, int x, int y, int buttons);
	void (*tick)(struct win *w);
	void (*command)(struct win *w, int id);
	void (*resize)(struct win *w, int cw, int ch);	/* client size changed */
	void (*action)(const char *arg);		/* APP_ACTION */
};

enum win_state { WS_NORMAL, WS_MIN, WS_MAX };

struct win {
	bool used, closing, modal;
	int id;
	const struct app *app;
	char title[40];
	int x, y;			/* frame position */
	int cw, ch;			/* client size when not maximized */
	enum win_state state, prev_state;
	struct gfx_canvas *pix;		/* a pixel client: the app draws here */
	int pix_idx;			/* its steel client slot, -1 = none */
	unsigned long next_tick;
	struct steel_surface client;	/* what the app paints into */
	steel_cell_t cells[DESK_W * DESK_H];
	u8 priv[WIN_PRIV_SIZE] __aligned(8);	/* the program's own state */
};

/* typed access to w->priv, with the size checked at build time */
#define DESK_PRIV(w, type) ({						\
	_Static_assert(sizeof(type) <= WIN_PRIV_SIZE, #type " too big");\
	(type *)(void *)(w)->priv; })

/* ---- the desktop API ------------------------------------------------- */

struct desk_backend {
	/* timeout_ms: 0 polls, < 0 waits forever; 0 on success */
	int (*wait_event)(struct input_event *ev, long timeout_ms);
	void (*present)(const steel_cell_t *cells, int w, int h);
	unsigned long (*now_ms)(void);
	/* pixel display only (all three may be NULL): see steel/pixdev.h */
	int (*client_set)(int idx, int x, int y, int w, int h, const u32 *px,
			  int stride);
	void (*client_dirty)(int idx, int x, int y, int w, int h);
	void (*client_clear)(int idx);
};

/* the program that runs the desktop says whether the display has pixels */
void desk_set_pixels(bool on, int off_x, int off_y);
bool desk_pixels(void);
/*
 * A program with real pixels: point w->pix at a canvas of exactly the client
 * size (cw * 12 by ch * 28 pixels) in ->open(). While the window is visible
 * the desktop shows the canvas in place of text. Call desk_px_dirty() after
 * drawing; desk_pointer_px() gives the pointer inside the canvas.
 */
void desk_px_dirty(struct win *w, int x, int y, int cw, int ch);
bool desk_pointer_px(const struct win *w, int *x, int *y);

void desk_run(const struct desk_backend *backend, const char *start);
void desk_quit(void);

struct win *desk_launch(const struct app *app, const char *arg);
struct win *desk_launch_sized(const struct app *app, const char *arg, int cw,
			      int ch);
void desk_close(struct win *w);
void desk_set_title(struct win *w, const char *title);
void desk_set_client_size(struct win *w, int cw, int ch);
bool desk_is_active(const struct win *w);
struct win *desk_find(int id);
const struct app *desk_find_app(const char *name);
unsigned long desk_now(void);			/* milliseconds */

/* dialogs; @done runs with the window that asked, if it still exists */
void desk_msgbox(const char *title, const char *text);
void desk_confirm(struct win *w, const char *title, const char *text,
		  void (*ok)(struct win *w));
void desk_prompt(struct win *w, const char *title, const char *label,
		 const char *initial,
		 void (*done)(struct win *w, const char *text));

u32 desk_rand(void);
void desk_seed(u32 seed);

/* ---- drawing helpers for programs ------------------------------------ */

/* "text" on a gray bar, right aligned number etc. are done with snprintf */
void desk_draw_icon(struct steel_surface *s, int x, int y, int cell_w,
		    const struct app *app, bool selected);
void desk_draw_button(struct steel_surface *s, int x, int y, const char *label,
		      bool focused);
void desk_draw_sunken(struct steel_surface *s, int x, int y, int w, int h);

/* ---- the programs ---------------------------------------------------- */

extern const struct app app_hub, app_files, app_notes, app_calc,
	app_clock, app_sysinfo, app_tasks, app_term, app_mines, app_reversi,
	app_snake, app_bricks, app_chess;
extern const struct app *const desk_apps[];
extern const int desk_apps_count;

/* the qubix program "desk" lives in apps/desk_main.c */
extern const struct desk_backend desk_qubix_backend;

#endif /* _DESK_DESK_H */
