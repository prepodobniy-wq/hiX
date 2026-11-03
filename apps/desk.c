// SPDX-License-Identifier: GPL-2.0-only
/*
 * desk: the window manager.
 *
 * One screen surface, a stack of windows, one open menu at a time. Every
 * event goes through dispatch_key() or dispatch_mouse(); after the events
 * (and the timer ticks of the games) the whole screen is recomposed and
 * presented, which at 80x25 cells costs next to nothing.
 *
 * Windows are removed lazily: desk_close() only marks one as closing, and
 * reap() frees it once the callback that asked is finished.
 */
#include <hix/kernel.h>
#include <hix/version.h>
#include <desk/desk.h>
#include <steel/pixdev.h>

#define BORDER_SLOTS	2		/* windows kept free for dialogs */
#define DBLCLICK_MS	500
#define ICON_SLOT_W	14
#define ICON_ROWS	3

/* what a menu action refers to when it is not one of the app's menus */
#define MENU_SYS	99

static struct win wins[DESK_MAX_WINDOWS];
static int zorder[DESK_MAX_WINDOWS];	/* indices into wins, bottom first */
static int nz;
static int next_id = 1;

static steel_cell_t screen_cells[DESK_W * DESK_H];
static struct steel_surface screen;

static const struct desk_backend *be;
static bool pixel_mode;
static int pix_off_x, pix_off_y;
static int mpx, mpy;			/* pointer in pixels, pixel display only */
static int px_owner[PIX_CLIENTS];	/* window id that owns each client slot */
static int px_ox[PIX_CLIENTS], px_oy[PIX_CLIENTS];
#define PIX_CELL_W_	12
#define PIX_CELL_H_	28
static bool running;
static unsigned long now;
static u32 rng = 2463534242u;

static int mx = DESK_W / 2, my = DESK_H / 2, mbuttons;
static bool mouse_seen;
static int capture_id;
static struct {
	bool active;
	int id, dx, dy;
} drag;
static struct {
	unsigned long t;
	int x, y, id;
} lastclick;

static struct {
	bool active;		/* the menu bar has the keyboard */
	bool open;		/* a drop-down is showing */
	int win_id;
	int menu;		/* index, or MENU_SYS */
	int item;
} mn;

static const struct menu_item sys_items[] = {
	{ "Restore", CMD_RESTORE, NULL },
	{ "Minimize", CMD_MINIMIZE, NULL },
	{ "Maximize", CMD_MAXIMIZE, NULL },
	{ "", 0, NULL },
	{ "Close", CMD_CLOSE, "Alt+F4" },
};
static const struct menu sys_menu = { "-", sys_items, ARRAY_SIZE(sys_items) };

const struct menu_item desk_help_items[] = {
	{ "About hix Desktop...", CMD_ABOUT, NULL },
};

/* ---- small services -------------------------------------------------- */

void desk_seed(u32 seed)
{
	rng = seed ? seed : 2463534242u;
}

u32 desk_rand(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return rng;
}

unsigned long desk_now(void)
{
	return now;
}

void desk_quit(void)
{
	running = false;
}

const struct app *desk_find_app(const char *name)
{
	int i;

	for (i = 0; i < desk_apps_count; i++)
		if (!strcmp(desk_apps[i]->name, name))
			return desk_apps[i];
	return NULL;
}

struct win *desk_find(int id)
{
	int i;

	for (i = 0; i < DESK_MAX_WINDOWS; i++)
		if (wins[i].used && !wins[i].closing && wins[i].id == id)
			return &wins[i];
	return NULL;
}

/* ---- window list ----------------------------------------------------- */

static int menu_rows(const struct win *w)
{
	return w->app->nmenus ? 1 : 0;
}

static enum win_state effective_state(const struct win *w)
{
	return w->state == WS_MIN ? w->prev_state : w->state;
}

/* frame rectangle on the screen */
static void win_geom(const struct win *w, int *x, int *y, int *fw, int *fh)
{
	if (effective_state(w) == WS_MAX && w->state != WS_MIN) {
		*x = 0;
		*y = 0;
		*fw = DESK_W;
		*fh = DESK_H;
		return;
	}
	*x = w->x;
	*y = w->y;
	*fw = w->cw + 2;
	*fh = w->ch + 2 + menu_rows(w);
}

static void apply_geometry(struct win *w)
{
	int cw = w->cw, ch = w->ch;

	if (effective_state(w) == WS_MAX) {
		cw = DESK_W - 2;
		ch = DESK_H - 2 - menu_rows(w);
	}
	if (cw != w->client.w || ch != w->client.h) {
		w->client.w = cw;
		w->client.h = ch;
		if (w->app->resize)
			w->app->resize(w, cw, ch);
	}
}

static void clamp_position(struct win *w)
{
	int fw = w->cw + 2, fh = w->ch + 2 + menu_rows(w);

	w->x = max(0, min(w->x, DESK_W - fw));
	w->y = max(0, min(w->y, DESK_H - fh));
}

static struct win *top_visible(void)
{
	int i;

	for (i = nz - 1; i >= 0; i--) {
		struct win *w = &wins[zorder[i]];

		if (!w->closing && w->state != WS_MIN)
			return w;
	}
	return NULL;
}

bool desk_is_active(const struct win *w)
{
	return top_visible() == w;
}

static struct win *modal_window(void)
{
	int i;

	for (i = 0; i < DESK_MAX_WINDOWS; i++)
		if (wins[i].used && !wins[i].closing && wins[i].modal)
			return &wins[i];
	return NULL;
}

static void z_remove(int idx)
{
	int i, j;

	for (i = 0; i < nz; i++) {
		if (zorder[i] == idx) {
			for (j = i; j < nz - 1; j++)
				zorder[j] = zorder[j + 1];
			nz--;
			return;
		}
	}
}

static void raise_win(struct win *w)
{
	int idx = (int)(w - wins);

	z_remove(idx);
	zorder[nz++] = idx;
}

static void set_state(struct win *w, enum win_state s)
{
	if (s == w->state)
		return;
	if (s == WS_MIN)
		w->prev_state = w->state;
	w->state = s;
	apply_geometry(w);
}

static void restore_win(struct win *w)
{
	if (w->state == WS_MIN)
		set_state(w, w->prev_state);
	else if (w->state == WS_MAX)
		set_state(w, WS_NORMAL);
	raise_win(w);
}

static int count_windows(void)
{
	int i, n = 0;

	for (i = 0; i < DESK_MAX_WINDOWS; i++)
		n += wins[i].used;
	return n;
}

static struct win *create_window(const struct app *app, int cw, int ch,
				 bool dialog)
{
	struct win *w = NULL;
	int i, n, fw, fh;

	if (!dialog && count_windows() >= DESK_MAX_WINDOWS - BORDER_SLOTS)
		return NULL;
	for (i = 0; i < DESK_MAX_WINDOWS; i++) {
		if (!wins[i].used) {
			w = &wins[i];
			break;
		}
	}
	if (!w)
		return NULL;

	n = count_windows();
	memset(w, 0, sizeof(*w));
	w->used = true;
	w->pix_idx = -1;
	w->id = next_id++;
	w->app = app;
	strlcpy(w->title, app->title, sizeof(w->title));
	w->cw = max(4, min(cw, DESK_W - 2));
	w->ch = max(1, min(ch, DESK_H - 3 - (app->nmenus ? 1 : 0)));
	steel_surface_init(&w->client, w->cw, w->ch, w->cells);

	fw = w->cw + 2;
	fh = w->ch + 2 + menu_rows(w);
	w->x = (DESK_W - fw) / 2 + (n % 5 - 2) * 2;
	w->y = (DESK_H - fh) / 2 + (n % 5 - 2);
	if (app->flags & APP_ROOT)
		w->x = w->y = 0;
	clamp_position(w);
	w->next_tick = now + app->tick_ms;
	zorder[nz++] = (int)(w - wins);
	return w;
}

struct win *desk_launch_sized(const struct app *app, const char *arg, int cw,
			      int ch)
{
	struct win *w;

	w = create_window(app, cw, ch, app->flags & APP_DIALOG);
	if (!w) {
		if (!(app->flags & APP_DIALOG))
			desk_msgbox("hix Desktop", "Too many windows are open.\n"
				    "Close one and try again.");
		return NULL;
	}
	if (app->open)
		app->open(w, arg);
	return w;
}

struct win *desk_launch(const struct app *app, const char *arg)
{
	int i;

	if (app->flags & APP_ACTION) {
		if (app->action)
			app->action(arg);
		return NULL;
	}
	if (app->flags & APP_SINGLE) {
		for (i = 0; i < DESK_MAX_WINDOWS; i++) {
			if (wins[i].used && !wins[i].closing && wins[i].app == app) {
				restore_win(&wins[i]);
				return &wins[i];
			}
		}
	}
	return desk_launch_sized(app, arg, app->cw, app->ch);
}

void desk_close(struct win *w)
{
	w->closing = true;
	if (w->modal)
		w->modal = false;
}

void desk_set_title(struct win *w, const char *title)
{
	strlcpy(w->title, title, sizeof(w->title));
}

void desk_set_client_size(struct win *w, int cw, int ch)
{
	w->cw = max(4, min(cw, DESK_W - 2));
	w->ch = max(1, min(ch, DESK_H - 3 - menu_rows(w)));
	w->client.w = w->cw;
	w->client.h = w->ch;
	if (w->state == WS_MAX)
		w->state = WS_NORMAL;
	clamp_position(w);
}

static void reap(void)
{
	int i;

	for (i = 0; i < DESK_MAX_WINDOWS; i++) {
		struct win *w = &wins[i];

		if (!w->used || !w->closing)
			continue;
		if (w->app->close)
			w->app->close(w);
		if (capture_id == w->id)
			capture_id = 0;
		if (drag.active && drag.id == w->id)
			drag.active = false;
		if (mn.active && mn.win_id == w->id)
			mn.active = false;
		z_remove(i);
		w->used = false;
	}
}

/* ---- drawing helpers for programs ------------------------------------ */

void desk_draw_button(struct steel_surface *s, int x, int y, const char *label,
		      bool focused)
{
	u8 attr = focused ? A_SEL : A_WIN;
	int len = (int)strlen(label);

	steel_rect(s, x, y, len + 4, 1, ' ', attr);
	steel_put(s, x, y, '[', attr);
	steel_puts(s, x + 2, y, label, attr);
	steel_put(s, x + len + 3, y, ']', attr);
}

/* a "sunken" panel: dark gray frame inside a white field */
void desk_draw_sunken(struct steel_surface *s, int x, int y, int w, int h)
{
	steel_rect(s, x, y, w, h, ' ', A_WIN);
	steel_box(s, x - 1, y - 1, w + 2, h + 2, STEEL_BOX_SINGLE,
		  STEEL_ATTR(STEEL_DGRAY, STEEL_LGRAY));
}

/* icon cell: two rows of glyph, then the label; @cell_w wide */
void desk_draw_icon(struct steel_surface *s, int x, int y, int cell_w,
		    const struct app *app, bool selected)
{
	char label[16];
	int len, i;
	u8 lattr = selected ? A_SEL : A_WIN;

	for (i = 0; i < 2; i++) {
		int gx = x + (cell_w - 6) / 2;

		steel_puts(s, gx, y + i, app->icon[i], app->icon_attr[i]);
	}
	strlcpy(label, app->title, sizeof(label));
	len = (int)strlen(label);
	if (len > cell_w)
		len = cell_w;
	label[len] = '\0';
	steel_rect(s, x, y + 2, cell_w, 1, ' ', A_WIN);
	steel_puts(s, x + (cell_w - len) / 2, y + 2, label, lattr);
}

/* ---- menus ----------------------------------------------------------- */

static const struct menu *menu_of(const struct win *w, int idx)
{
	return idx == MENU_SYS ? &sys_menu : &w->app->menus[idx];
}

/* x of the first letter of menu title @idx, screen coordinates */
static int menu_title_x(const struct win *w, int idx)
{
	int x, y, fw, fh, i, pos;

	win_geom(w, &x, &y, &fw, &fh);
	pos = x + 2;
	for (i = 0; i < idx; i++)
		pos += (int)strlen(w->app->menus[i].title) + 2;
	return pos;
}

static void menu_geometry(const struct win *w, int *dx, int *dy, int *dw,
			  int *dh)
{
	const struct menu *m = menu_of(w, mn.menu);
	int fx, fy, fw, fh, i, width = 0;

	win_geom(w, &fx, &fy, &fw, &fh);
	for (i = 0; i < m->nitems; i++) {
		int len = (int)strlen(m->items[i].label);

		if (m->items[i].accel)
			len += 3 + (int)strlen(m->items[i].accel);
		width = max(width, len);
	}
	*dw = width + 4;
	*dh = m->nitems + 2;
	if (mn.menu == MENU_SYS) {
		*dx = fx;
		*dy = fy + 1;
	} else {
		*dx = menu_title_x(w, mn.menu) - 1;
		*dy = fy + 2;
	}
	*dx = max(0, min(*dx, DESK_W - *dw));
	*dy = max(0, min(*dy, DESK_H - *dh));
}

static bool item_usable(const struct win *w, const struct menu_item *it)
{
	if (!it->id)
		return false;
	if (it->id == CMD_RESTORE)
		return w->state != WS_NORMAL;
	if (it->id == CMD_MAXIMIZE)
		return (w->app->flags & APP_RESIZABLE) && w->state != WS_MAX;
	if (it->id == CMD_MINIMIZE)
		return !(w->app->flags & APP_DIALOG);
	return true;
}

static int step_item(const struct win *w, const struct menu *m, int cur,
		     int dir)
{
	int i, n = m->nitems;

	for (i = 1; i <= n; i++) {
		int k = ((cur + dir * i) % n + n) % n;

		if (item_usable(w, &m->items[k]))
			return k;
	}
	return cur;
}

static void menu_start(struct win *w, int menu, bool open)
{
	const struct menu *m;

	mn.active = true;
	mn.win_id = w->id;
	mn.menu = menu;
	mn.open = open;
	m = menu_of(w, menu);
	mn.item = open ? step_item(w, m, -1, 1) : -1;
}

static void run_command(struct win *w, int id);

static void menu_execute(void)
{
	struct win *w = desk_find(mn.win_id);
	const struct menu *m;
	int id;

	if (!w || !mn.open || mn.item < 0) {
		mn.active = false;
		return;
	}
	m = menu_of(w, mn.menu);
	id = m->items[mn.item].id;
	mn.active = false;
	if (id)
		run_command(w, id);
}

static void menu_key(int key, u8 mods)
{
	struct win *w = desk_find(mn.win_id);
	const struct menu *m;
	int n;

	if (!w) {
		mn.active = false;
		return;
	}
	n = w->app->nmenus;
	m = menu_of(w, mn.menu);

	switch (key) {
	case 27:
	case KEY_F(10):
		mn.active = false;
		return;
	case KEY_LEFT:
	case KEY_RIGHT:
		if (mn.menu == MENU_SYS || !n)
			return;
		mn.menu = (mn.menu + (key == KEY_LEFT ? n - 1 : 1)) % n;
		m = menu_of(w, mn.menu);
		mn.item = mn.open ? step_item(w, m, -1, 1) : -1;
		return;
	case KEY_DOWN:
	case KEY_UP:
		if (!mn.open) {
			mn.open = true;
			mn.item = step_item(w, m, -1, 1);
			return;
		}
		mn.item = step_item(w, m, mn.item, key == KEY_DOWN ? 1 : -1);
		return;
	case '\n':
		if (!mn.open) {
			mn.open = true;
			mn.item = step_item(w, m, -1, 1);
			return;
		}
		menu_execute();
		return;
	default:
		break;
	}
	(void)mods;

	/* a letter picks a drop-down item by its first letter */
	if (mn.open && key < 0x80) {
		int i, k, c = key | 0x20;

		for (i = 1; i <= m->nitems; i++) {
			k = (mn.item + i) % m->nitems;
			if (item_usable(w, &m->items[k]) &&
			    (m->items[k].label[0] | 0x20) == c) {
				mn.item = k;
				return;
			}
		}
	}
}

static bool menu_hotkey(struct win *w, int key)
{
	int i, c = key | 0x20;

	for (i = 0; i < w->app->nmenus; i++) {
		if ((w->app->menus[i].title[0] | 0x20) == c) {
			menu_start(w, i, true);
			return true;
		}
	}
	return false;
}

/* ---- commands -------------------------------------------------------- */

static void quit_ok(struct win *w)
{
	(void)w;
	desk_quit();
}

static void request_close(struct win *w)
{
	if (w->app->flags & APP_ROOT) {
		desk_confirm(w, "Exit hix Desktop",
			     "This will end your hix Desktop session.\n"
			     "Press OK to leave, Cancel to stay.", quit_ok);
		return;
	}
	if (w->app->close_request && !w->app->close_request(w))
		return;
	desk_close(w);
}

static void show_about(void)
{
	char text[160];

	snprintf(text, sizeof(text),
		 "hix Desktop 0.3\n\n"
		 "A classic windowed shell for the steel display.\n"
		 "Kernel: %s %s  (aether + qubix + steel)", HIX_NAME,
		 HIX_RELEASE);
	desk_msgbox("About hix Desktop", text);
}

static void run_command(struct win *w, int id)
{
	switch (id) {
	case CMD_CLOSE:
		request_close(w);
		return;
	case CMD_ABOUT:
		show_about();
		return;
	case CMD_RESTORE:
		restore_win(w);
		return;
	case CMD_MINIMIZE:
		if (!(w->app->flags & APP_DIALOG))
			set_state(w, WS_MIN);
		return;
	case CMD_MAXIMIZE:
		if (w->app->flags & APP_RESIZABLE)
			set_state(w, WS_MAX);
		return;
	default:
		break;
	}
	if (id < CMD_FIRST_WM && w->app->command)
		w->app->command(w, id);
}

/* ---- painting -------------------------------------------------------- */

static void draw_menu_bar(const struct win *w, int x, int y, int fw)
{
	int i;

	steel_rect(&screen, x + 1, y, fw - 2, 1, ' ', A_WIN);
	for (i = 0; i < w->app->nmenus; i++) {
		int tx = menu_title_x(w, i);
		bool sel = mn.active && mn.win_id == w->id && mn.menu == i;
		u8 attr = sel ? A_SEL : A_WIN;
		const char *t = w->app->menus[i].title;

		steel_put(&screen, tx - 1, y, ' ', attr);
		steel_puts(&screen, tx, y, t, attr);
		steel_put(&screen, tx + (int)strlen(t), y, ' ', attr);
	}
}

static void draw_frame(const struct win *w, bool active)
{
	u8 tattr = active ? A_TITLE : A_TITLE_OFF;
	int x, y, fw, fh, len, tx, room;
	bool dialog = w->app->flags & APP_DIALOG;
	bool resizable = w->app->flags & APP_RESIZABLE;
	char title[40];

	win_geom(w, &x, &y, &fw, &fh);
	steel_rect(&screen, x, y, fw, fh, ' ', A_GRAY);
	steel_box(&screen, x, y, fw, fh, STEEL_BOX_SINGLE, A_GRAY);
	steel_rect(&screen, x, y, fw, 1, ' ', tattr);

	/* control box on the left, minimize/maximize on the right */
	steel_put(&screen, x, y, ' ', A_GRAY);
	steel_put(&screen, x + 1, y, 0xF0, A_GRAY);
	steel_put(&screen, x + 2, y, ' ', A_GRAY);
	room = fw - 4 - 1;
	if (!dialog) {
		int bx = x + fw - 4;

		if (resizable) {
			steel_puts(&screen, bx, y, w->state == WS_MAX ?
				   " \022 " : " \036 ", A_GRAY);
			bx -= 3;
		}
		steel_puts(&screen, bx, y, " \037 ", A_GRAY);
		room = bx - (x + 3) - 1;
	}

	strlcpy(title, w->title, sizeof(title));
	len = (int)strlen(title);
	if (len > room) {
		len = max(room, 0);
		title[len] = '\0';
	}
	tx = x + 3 + (room - len) / 2 + 1;
	steel_puts(&screen, tx, y, title, tattr);

	if (menu_rows(w))
		draw_menu_bar(w, x, y + 1, fw);
}

static void draw_dropdown(const struct win *w)
{
	const struct menu *m = menu_of(w, mn.menu);
	int dx, dy, dw, dh, i;

	menu_geometry(w, &dx, &dy, &dw, &dh);
	steel_rect(&screen, dx, dy, dw, dh, ' ', A_WIN);
	steel_box(&screen, dx, dy, dw, dh, STEEL_BOX_SINGLE, A_WIN);
	for (i = 0; i < m->nitems; i++) {
		const struct menu_item *it = &m->items[i];
		int row = dy + 1 + i;
		bool usable = item_usable(w, it);
		u8 attr = i == mn.item ? A_SEL : (usable ? A_WIN : A_DIM);

		if (!it->id) {
			steel_hline(&screen, dx + 1, row, dw - 2, 0xC4, A_WIN);
			continue;
		}
		steel_rect(&screen, dx + 1, row, dw - 2, 1, ' ', attr);
		steel_puts(&screen, dx + 2, row, it->label, attr);
		if (it->accel)
			steel_puts(&screen, dx + dw - 2 - (int)strlen(it->accel),
				   row, it->accel, attr);
	}
}

static void minimized_origin(const struct win *w, int *ix, int *iy)
{
	int i, slot = 0;

	for (i = 0; i < DESK_MAX_WINDOWS; i++) {
		if (&wins[i] == w)
			break;
		if (wins[i].used && !wins[i].closing && wins[i].state == WS_MIN)
			slot++;
	}
	*ix = 1 + slot * ICON_SLOT_W;
	*iy = DESK_H - ICON_ROWS;
}

static void draw_minimized(const struct win *w)
{
	int ix, iy, i;
	char label[16];

	minimized_origin(w, &ix, &iy);
	steel_rect(&screen, ix, iy, ICON_SLOT_W - 2, ICON_ROWS, ' ', A_WIN);
	for (i = 0; i < 2; i++)
		steel_puts(&screen, ix + 3, iy + i, w->app->icon[i],
			   w->app->icon_attr[i]);
	strlcpy(label, w->title, sizeof(label));
	label[ICON_SLOT_W - 2] = '\0';
	steel_rect(&screen, ix, iy + 2, ICON_SLOT_W - 2, 1, ' ', A_GRAY);
	steel_puts(&screen, ix + 1, iy + 2, label, A_GRAY);
}

/* ---- pixel clients --------------------------------------------------- */

void desk_set_pixels(bool on, int off_x, int off_y)
{
	pixel_mode = on;
	pix_off_x = off_x;
	pix_off_y = off_y;
}

bool desk_pixels(void)
{
	return pixel_mode;
}

/* the window shows its canvas: fill its cells with holes and register it */
static void px_attach(struct win *w, int cx, int cy, bool *used)
{
	int idx = w->pix_idx, ox, oy;

	if (idx < 0) {
		for (idx = 0; idx < PIX_CLIENTS; idx++)
			if (!px_owner[idx])
				break;
		if (idx == PIX_CLIENTS)
			return;
		w->pix_idx = idx;
		px_owner[idx] = w->id;
		px_ox[idx] = px_oy[idx] = -1;
	}
	used[idx] = true;
	steel_fill(&w->client, 0, (u8)(0xA0 + idx));
	ox = pix_off_x + cx * PIX_CELL_W_;
	oy = pix_off_y + cy * PIX_CELL_H_;
	if (ox != px_ox[idx] || oy != px_oy[idx]) {
		if (be->client_set &&
		    !be->client_set(idx, ox, oy, w->pix->w, w->pix->h, w->pix->px,
				    w->pix->stride)) {
			px_ox[idx] = ox;
			px_oy[idx] = oy;
		}
	}
}

/* slots whose window is gone, hidden or minimized go back to steel */
static void px_release_unused(const bool *used)
{
	int i, j;

	for (i = 0; i < PIX_CLIENTS; i++) {
		if (!px_owner[i] || used[i])
			continue;
		if (be->client_clear)
			be->client_clear(i);
		px_owner[i] = 0;
		for (j = 0; j < DESK_MAX_WINDOWS; j++)
			if (wins[j].used && wins[j].pix_idx == i)
				wins[j].pix_idx = -1;
	}
}

void desk_px_dirty(struct win *w, int x, int y, int cw, int ch)
{
	if (pixel_mode && w->pix_idx >= 0 && be->client_dirty)
		be->client_dirty(w->pix_idx, x, y, cw, ch);
}

bool desk_pointer_px(const struct win *w, int *x, int *y)
{
	int wx, wy, fw, fh;

	if (!pixel_mode)
		return false;
	win_geom(w, &wx, &wy, &fw, &fh);
	*x = mpx - (pix_off_x + (wx + 1) * PIX_CELL_W_);
	*y = mpy - (pix_off_y + (wy + 1 + menu_rows(w)) * PIX_CELL_H_);
	return true;
}

static void compose(void)
{
	struct win *top = top_visible();
	bool used[PIX_CLIENTS] = { false };
	int i;

	steel_fill(&screen, ' ', A_DESKTOP);
	for (i = 0; i < DESK_MAX_WINDOWS; i++)
		if (wins[i].used && !wins[i].closing && wins[i].state == WS_MIN)
			draw_minimized(&wins[i]);

	for (i = 0; i < nz; i++) {
		struct win *w = &wins[zorder[i]];
		int x, y, fw, fh;

		if (w->closing || w->state == WS_MIN)
			continue;
		steel_fill(&w->client, ' ', A_WIN);
		if (w->app->paint)
			w->app->paint(w, &w->client);
		draw_frame(w, w == top);
		win_geom(w, &x, &y, &fw, &fh);
		if (pixel_mode && w->pix)
			px_attach(w, x + 1, y + 1 + menu_rows(w), used);
		steel_blit(&screen, x + 1, y + 1 + menu_rows(w), &w->client, 0, 0,
			   w->client.w, w->client.h);
	}
	px_release_unused(used);

	if (mn.active && mn.open) {
		struct win *w = desk_find(mn.win_id);

		if (w)
			draw_dropdown(w);
	}

	if (mouse_seen && !pixel_mode) {	/* the pointer: the cell, inverted */
		steel_cell_t c = steel_get(&screen, mx, my);
		u8 a = STEEL_AT(c);

		steel_put(&screen, mx, my, STEEL_CH(c),
			  (u8)(((a & 0x0F) << 4) | (a >> 4)));
	}
}

/* ---- hit testing ----------------------------------------------------- */

enum hit_part {
	H_NONE, H_ICON, H_CTRL, H_MIN, H_MAX, H_TITLE, H_MENUBAR, H_CLIENT,
	H_BORDER,
};

static struct win *hit_test(int px, int py, enum hit_part *part, int *arg)
{
	int i;

	*part = H_NONE;
	*arg = 0;
	for (i = nz - 1; i >= 0; i--) {
		struct win *w = &wins[zorder[i]];
		int x, y, fw, fh, cy;
		bool dialog, resizable;

		if (w->closing || w->state == WS_MIN)
			continue;
		win_geom(w, &x, &y, &fw, &fh);
		if (px < x || px >= x + fw || py < y || py >= y + fh)
			continue;
		dialog = w->app->flags & APP_DIALOG;
		resizable = w->app->flags & APP_RESIZABLE;
		cy = y + 1 + menu_rows(w);

		if (py == y) {
			int bx = x + fw - 4;

			*part = H_TITLE;
			if (px <= x + 2) {
				*part = H_CTRL;
			} else if (!dialog && px >= bx && px < bx + 3) {
				*part = resizable ? H_MAX : H_MIN;
			} else if (!dialog && resizable && px >= bx - 3 && px < bx) {
				*part = H_MIN;
			}
		} else if (menu_rows(w) && py == y + 1 && px > x && px < x + fw - 1) {
			int k;

			*part = H_MENUBAR;
			*arg = -1;
			for (k = 0; k < w->app->nmenus; k++) {
				int tx = menu_title_x(w, k);

				if (px >= tx - 1 &&
				    px <= tx + (int)strlen(w->app->menus[k].title))
					*arg = k;
			}
		} else if (px > x && px < x + fw - 1 && py >= cy && py < y + fh - 1) {
			*part = H_CLIENT;
		} else {
			*part = H_BORDER;
		}
		return w;
	}

	for (i = 0; i < DESK_MAX_WINDOWS; i++) {
		struct win *w = &wins[i];
		int ix, iy;

		if (!w->used || w->closing || w->state != WS_MIN)
			continue;
		minimized_origin(w, &ix, &iy);
		if (px >= ix && px < ix + ICON_SLOT_W - 2 && py >= iy &&
		    py < iy + ICON_ROWS) {
			*part = H_ICON;
			return w;
		}
	}
	return NULL;
}

/* ---- keyboard -------------------------------------------------------- */

static void move_window(struct win *w, int dx, int dy)
{
	if (w->state != WS_NORMAL)
		return;
	w->x += dx;
	w->y += dy;
	clamp_position(w);
}

static void cycle_windows(void)
{
	int i;

	for (i = 0; i < nz; i++) {
		struct win *w = &wins[zorder[i]];

		if (!w->closing && !w->modal) {
			restore_win(w);
			return;
		}
	}
}

static void dispatch_key(const struct input_event *ev)
{
	struct win *top = top_visible();
	struct win *modal = modal_window();
	int key = ev->key;
	u8 mods = ev->mods;

	if (mn.active) {
		menu_key(key, mods);
		return;
	}
	if (modal)
		top = modal;
	if (!top)
		return;

	if (mods & MOD_ALT) {
		switch (key) {
		case KEY_F(4):
			request_close(top);
			return;
		case '\t':
			if (!modal)
				cycle_windows();
			return;
		case KEY_LEFT:
			move_window(top, -1, 0);
			return;
		case KEY_RIGHT:
			move_window(top, 1, 0);
			return;
		case KEY_UP:
			move_window(top, 0, -1);
			return;
		case KEY_DOWN:
			move_window(top, 0, 1);
			return;
		case ' ':
			menu_start(top, MENU_SYS, true);
			return;
		default:
			if (key < 0x80 && menu_hotkey(top, key))
				return;
			break;
		}
	}
	if (key == KEY_F(10) && top->app->nmenus) {
		menu_start(top, 0, false);
		return;
	}
	if (key == 27 && (mods & MOD_CTRL) && !modal) {	/* Ctrl+Esc: task list */
		struct win *pm = desk_find_app("hub") ?
			NULL : NULL;

		(void)pm;
		cycle_windows();
		return;
	}
	if (top->app->key && top->app->key(top, key, mods))
		return;
}

/* ---- mouse ----------------------------------------------------------- */

static bool is_double_click(int id)
{
	bool dbl = lastclick.id == id && now - lastclick.t < DBLCLICK_MS &&
		   abs(mx - lastclick.x) <= 1 && abs(my - lastclick.y) <= 1;

	lastclick.t = dbl ? 0 : now;
	lastclick.x = mx;
	lastclick.y = my;
	lastclick.id = id;
	return dbl;
}

static void client_event(struct win *w, int ev, int buttons)
{
	int x, y, fw, fh;

	if (!w->app->mouse)
		return;
	win_geom(w, &x, &y, &fw, &fh);
	w->app->mouse(w, ev, mx - (x + 1), my - (y + 1 + menu_rows(w)), buttons);
}

static void mouse_in_menu(const struct input_event *ev, bool pressed)
{
	struct win *w = desk_find(mn.win_id);
	const struct menu *m;
	int dx, dy, dw, dh;

	if (!w) {
		mn.active = false;
		return;
	}
	if (mn.open) {
		m = menu_of(w, mn.menu);
		menu_geometry(w, &dx, &dy, &dw, &dh);
		if (mx > dx && mx < dx + dw - 1 && my > dy && my < dy + dh - 1) {
			int item = my - dy - 1;

			if (item_usable(w, &m->items[item]))
				mn.item = item;
			if (pressed && item_usable(w, &m->items[item])) {
				mn.item = item;
				menu_execute();
			}
			return;
		}
	}
	if (pressed) {
		enum hit_part part;
		int arg;
		struct win *hw = hit_test(mx, my, &part, &arg);

		if (hw == w && part == H_MENUBAR && arg >= 0) {
			menu_start(w, arg, true);
		} else if (hw == w && part == H_CTRL && mn.menu == MENU_SYS) {
			mn.active = false;
		} else {
			mn.active = false;
		}
	} else if (mn.open && mn.menu != MENU_SYS) {	/* slide to a neighbour */
		enum hit_part part;
		int arg;
		struct win *hw = hit_test(mx, my, &part, &arg);

		if (hw == w && part == H_MENUBAR && arg >= 0 && arg != mn.menu)
			menu_start(w, arg, true);
	}
	(void)ev;
}

static void dispatch_mouse(const struct input_event *ev)
{
	int old = mbuttons;
	bool pressed, released, left_down;
	struct win *w, *modal;
	enum hit_part part;
	int arg;

	mx = ev->x;
	my = ev->y;
	mpx = ev->px;
	mpy = ev->py;
	mbuttons = ev->buttons;
	mouse_seen = true;
	pressed = (mbuttons & ~old) != 0;
	released = (old & ~mbuttons) != 0;
	left_down = (mbuttons & ~old & MOUSE_LEFT) != 0;

	if (mn.active) {
		mouse_in_menu(ev, pressed);
		return;
	}
	if (drag.active) {
		w = desk_find(drag.id);
		if (!w || !(mbuttons & MOUSE_LEFT)) {
			drag.active = false;
		} else {
			w->x = mx - drag.dx;
			w->y = my - drag.dy;
			clamp_position(w);
		}
		return;
	}
	if (capture_id) {
		w = desk_find(capture_id);
		if (!w) {
			capture_id = 0;
		} else {
			if (released || !mbuttons)
				client_event(w, MEV_UP, old);
			else if (!pressed)
				client_event(w, MEV_MOVE, mbuttons);
			if (!mbuttons)
				capture_id = 0;
			return;
		}
	}

	modal = modal_window();
	w = hit_test(mx, my, &part, &arg);

	if (!pressed) {
		if (w && part == H_CLIENT && w == top_visible())
			client_event(w, MEV_MOVE, mbuttons);
		return;
	}
	if (modal && w != modal)
		return;
	if (!w) {
		return;
	}
	if (part == H_ICON) {
		restore_win(w);
		return;
	}
	if (w != top_visible())
		raise_win(w);

	switch (part) {
	case H_CTRL:
		if (!left_down)
			break;
		if (is_double_click(w->id))
			request_close(w);
		else
			menu_start(w, MENU_SYS, true);
		break;
	case H_MIN:
		if (left_down)
			run_command(w, CMD_MINIMIZE);
		break;
	case H_MAX:
		if (left_down)
			run_command(w, w->state == WS_MAX ? CMD_RESTORE :
				    CMD_MAXIMIZE);
		break;
	case H_TITLE:
		if (!left_down)
			break;
		if (is_double_click(w->id)) {
			if (w->app->flags & APP_RESIZABLE)
				run_command(w, w->state == WS_MAX ? CMD_RESTORE :
					    CMD_MAXIMIZE);
		} else if (w->state == WS_NORMAL) {
			drag.active = true;
			drag.id = w->id;
			drag.dx = mx - w->x;
			drag.dy = my - w->y;
		}
		break;
	case H_MENUBAR:
		if (left_down && arg >= 0)
			menu_start(w, arg, true);
		break;
	case H_CLIENT:
		capture_id = w->id;
		client_event(w, MEV_DOWN, mbuttons);
		if (left_down && is_double_click(w->id))
			client_event(w, MEV_DBL, mbuttons);
		break;
	default:
		break;
	}
}

/* ---- the loop -------------------------------------------------------- */

static long next_timeout(void)
{
	long t = 1000;
	int i;

	for (i = 0; i < DESK_MAX_WINDOWS; i++) {
		struct win *w = &wins[i];
		long left;

		if (!w->used || w->closing || w->state == WS_MIN ||
		    !w->app->tick_ms)
			continue;
		left = (long)(w->next_tick - now);
		t = min(t, max(left, 1L));
	}
	return t;
}

static void run_ticks(void)
{
	int i;

	for (i = 0; i < DESK_MAX_WINDOWS; i++) {
		struct win *w = &wins[i];

		if (!w->used || w->closing || !w->app->tick_ms)
			continue;
		if (w->state == WS_MIN) {	/* no catching up after a pause */
			w->next_tick = now + w->app->tick_ms;
			continue;
		}
		if ((long)(now - w->next_tick) < 0)
			continue;
		if (w->app->tick)
			w->app->tick(w);
		w->next_tick += w->app->tick_ms;
		if ((long)(now - w->next_tick) > 0)	/* fell behind: skip */
			w->next_tick = now + w->app->tick_ms;
	}
}

static void dispatch(const struct input_event *ev)
{
	if (ev->type == INPUT_KEY)
		dispatch_key(ev);
	else if (ev->type == INPUT_MOUSE)
		dispatch_mouse(ev);
}

void desk_run(const struct desk_backend *backend, const char *start)
{
	const struct app *first = NULL;
	int i;

	memset(px_owner, 0, sizeof(px_owner));
	memset(px_ox, 0, sizeof(px_ox));
	memset(px_oy, 0, sizeof(px_oy));

	be = backend;
	now = be->now_ms();
	memset(wins, 0, sizeof(wins));
	nz = 0;
	mn.active = false;
	capture_id = 0;
	drag.active = false;
	mouse_seen = false;
	steel_surface_init(&screen, DESK_W, DESK_H, screen_cells);

	desk_launch(&app_hub, NULL);
	if (start && *start)
		first = desk_find_app(start);
	if (first)
		desk_launch(first, NULL);
	else if (start && *start)
		desk_msgbox("hix Desktop", "No such program.");

	running = true;
	compose();
	be->present(screen.cells, DESK_W, DESK_H);

	while (running) {
		struct input_event ev;
		int budget = 32;

		if (be->wait_event(&ev, next_timeout()) == 0) {
			now = be->now_ms();
			dispatch(&ev);
			while (running && budget-- &&
			       be->wait_event(&ev, 0) == 0)
				dispatch(&ev);
		}
		now = be->now_ms();
		run_ticks();
		reap();
		compose();
		be->present(screen.cells, DESK_W, DESK_H);
	}

	for (i = 0; i < DESK_MAX_WINDOWS; i++)
		if (wins[i].used)
			wins[i].closing = true;
	reap();
}
