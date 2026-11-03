// SPDX-License-Identifier: GPL-2.0-only
/*
 * desk: Hub, the root window of the desktop.
 *
 * Programs are arranged in groups, each a framed box of icons, the way
 * the Hub shows its program groups. Arrow keys move the selection,
 * Enter or a double click starts the program, File/Run starts one by name.
 */
#include <hix/kernel.h>
#include <desk/desk.h>

#define ICON_W		12		/* the icon cell */
#define ICON_STEP	13
#define ICON_H		3
#define ROW_STEP	4

struct group {
	const char *title;
	const struct app *const *apps;
	int n;
};

static const struct app *const main_apps[] = {
	&app_files, &app_notes, &app_calc, &app_clock, &app_sysinfo,
	&app_tasks, &app_term,
};
static const struct app *const game_apps[] = {
	&app_chess, &app_mines, &app_reversi, &app_snake, &app_bricks,
};
static const struct group groups[] = {
	{ "Main", main_apps, ARRAY_SIZE(main_apps) },
	{ "Games", game_apps, ARRAY_SIZE(game_apps) },
};
#define NGROUPS ARRAY_SIZE(groups)

struct pm {
	int sel;			/* flat index over all groups */
};

enum { PM_RUN = 1 };

static const struct menu_item file_items[] = {
	{ "Run...", PM_RUN, NULL },
	{ "", 0, NULL },
	{ "Exit hix Desktop", CMD_CLOSE, "Alt+F4" },
};
static const struct menu menus[] = {
	{ "File", file_items, ARRAY_SIZE(file_items) },
	DESK_HELP_MENU,
};

static int total_icons(void)
{
	int i, n = 0;

	for (i = 0; i < (int)NGROUPS; i++)
		n += groups[i].n;
	return n;
}

static int per_row(int cw)
{
	return max(1, (cw - 4) / ICON_STEP);
}

static int group_rows(const struct group *g, int cw)
{
	int pr = per_row(cw);

	return (g->n + pr - 1) / pr;
}

/* top row of group @gi's box */
static int group_top(int gi, int cw)
{
	int i, y = 0;

	for (i = 0; i < gi; i++)
		y += group_rows(&groups[i], cw) * ROW_STEP + 3;
	return y;
}

/* client position of flat icon @idx; returns false when out of range */
static bool icon_pos(int idx, int cw, int *x, int *y, const struct app **app)
{
	int gi, pr = per_row(cw);

	for (gi = 0; gi < (int)NGROUPS; gi++) {
		const struct group *g = &groups[gi];

		if (idx < g->n) {
			*x = 2 + (idx % pr) * ICON_STEP;
			*y = group_top(gi, cw) + 1 + (idx / pr) * ROW_STEP;
			*app = g->apps[idx];
			return true;
		}
		idx -= g->n;
	}
	return false;
}

static void pm_open(struct win *w, const char *arg)
{
	struct pm *pm = DESK_PRIV(w, struct pm);

	(void)arg;
	pm->sel = 0;
}

static void pm_paint(struct win *w, struct steel_surface *c)
{
	struct pm *pm = DESK_PRIV(w, struct pm);
	const u8 gray = STEEL_ATTR(STEEL_BLACK, STEEL_LGRAY);
	int gi, i, idx = 0;

	steel_fill(c, ' ', A_WIN);
	for (gi = 0; gi < (int)NGROUPS; gi++) {
		const struct group *g = &groups[gi];
		int top = group_top(gi, c->w);
		int h = group_rows(g, c->w) * ROW_STEP + 2;
		int tl = (int)strlen(g->title);

		steel_box(c, 0, top, c->w, h, STEEL_BOX_SINGLE, gray);
		steel_rect(c, 1, top, c->w - 2, 1, ' ', gray);
		steel_puts(c, (c->w - tl) / 2, top, g->title, gray);
		for (i = 0; i < g->n; i++, idx++) {
			int x, y;
			const struct app *a;

			if (!icon_pos(idx, c->w, &x, &y, &a))
				break;
			desk_draw_icon(c, x, y, ICON_W, a,
				       idx == pm->sel && desk_is_active(w));
		}
	}
}

static void launch_sel(struct win *w)
{
	struct pm *pm = DESK_PRIV(w, struct pm);
	int x, y;
	const struct app *a;

	if (icon_pos(pm->sel, w->client.w, &x, &y, &a))
		desk_launch(a, NULL);
}

static void run_done(struct win *w, const char *text)
{
	char name[40];
	const struct app *a;
	int n = 0;

	(void)w;
	while (*text == ' ')
		text++;
	while (text[n] && text[n] != ' ' && n < (int)sizeof(name) - 1) {
		name[n] = (char)(text[n] | ((text[n] >= 'A' && text[n] <= 'Z') ?
					    0x20 : 0));
		n++;
	}
	name[n] = '\0';
	if (!n)
		return;
	a = desk_find_app(name);
	if (!a || a == &app_hub) {
		char msg[96];

		snprintf(msg, sizeof(msg), "Cannot find '%s'.\n"
			 "Type the name of a program, e.g. calc.", name);
		desk_msgbox("Run", msg);
		return;
	}
	desk_launch(a, text + n + (text[n] == ' '));
}

static void pm_command(struct win *w, int id)
{
	if (id == PM_RUN)
		desk_prompt(w, "Run", "Command Line:", "", run_done);
}

static bool pm_key(struct win *w, int key, u8 mods)
{
	struct pm *pm = DESK_PRIV(w, struct pm);
	int n = total_icons(), pr = per_row(w->client.w);

	(void)mods;
	switch (key) {
	case KEY_RIGHT:
		pm->sel = min(pm->sel + 1, n - 1);
		return true;
	case KEY_LEFT:
		pm->sel = max(pm->sel - 1, 0);
		return true;
	case KEY_DOWN:
		pm->sel = min(pm->sel + pr, n - 1);
		return true;
	case KEY_UP:
		pm->sel = max(pm->sel - pr, 0);
		return true;
	case KEY_HOME:
		pm->sel = 0;
		return true;
	case KEY_END:
		pm->sel = n - 1;
		return true;
	case '\n':
		launch_sel(w);
		return true;
	default:
		return false;
	}
}

static void pm_mouse(struct win *w, int ev, int x, int y, int buttons)
{
	struct pm *pm = DESK_PRIV(w, struct pm);
	int i, n = total_icons();

	if ((ev != MEV_DOWN && ev != MEV_DBL) || !(buttons & MOUSE_LEFT))
		return;
	for (i = 0; i < n; i++) {
		int ix, iy;
		const struct app *a;

		if (!icon_pos(i, w->client.w, &ix, &iy, &a))
			break;
		if (x >= ix && x < ix + ICON_W && y >= iy && y < iy + ICON_H) {
			pm->sel = i;
			if (ev == MEV_DBL)
				launch_sel(w);
			return;
		}
	}
}

const struct app app_hub = {
	.name = "hub",
	.title = "Hub",
	.icon = { "\xDA\xC4\xC4\xC4\xC4\xBF", "\xC0\xC4\xC4\xC4\xC4\xD9" },
	.icon_attr = { STEEL_ATTR(STEEL_BLUE, STEEL_WHITE),
		       STEEL_ATTR(STEEL_BLUE, STEEL_WHITE) },
	.cw = 74, .ch = 19,
	.flags = APP_ROOT | APP_RESIZABLE | APP_SINGLE,
	.menus = menus, .nmenus = ARRAY_SIZE(menus),
	.open = pm_open,
	.paint = pm_paint,
	.key = pm_key,
	.mouse = pm_mouse,
	.command = pm_command,
};
