// SPDX-License-Identifier: GPL-2.0-only
/*
 * desk: Task List. Shows what `ps` shows: the tasks, threads and qubix
 * processes of the system, refreshed every second. Up/Down scroll.
 */
#include <hix/kernel.h>
#include <qubix/libq.h>
#include <desk/desk.h>

#define TEXT_MAX	6000

struct tl {
	char text[TEXT_MAX];
	int top;
	int nlines;
};

enum { TL_REFRESH = 1 };

static const struct menu_item task_items[] = {
	{ "Refresh", TL_REFRESH, "F5" },
	{ "", 0, NULL },
	{ "Exit", CMD_CLOSE, "Alt+F4" },
};
static const struct menu menus[] = {
	{ "Task", task_items, ARRAY_SIZE(task_items) },
	DESK_HELP_MENU,
};

static void refresh(struct tl *t)
{
	long n = q_sysctl("hix.ps", t->text, sizeof(t->text) - 1);
	char *p;

	if (n < 0)
		n = 0;
	t->text[n] = '\0';
	t->nlines = 0;
	for (p = t->text; *p; p++)
		t->nlines += *p == '\n';
}

static void tl_open(struct win *w, const char *arg)
{
	(void)arg;
	refresh(DESK_PRIV(w, struct tl));
}

static void tl_paint(struct win *w, struct steel_surface *c)
{
	struct tl *t = DESK_PRIV(w, struct tl);
	const char *p = t->text;
	int row = -t->top;

	while (*p && row < c->h) {
		char line[100];
		int n = 0;

		while (p[n] && p[n] != '\n' && n < (int)sizeof(line) - 1) {
			line[n] = p[n];
			n++;
		}
		line[n] = '\0';
		if (row >= 0)
			steel_puts(c, 1, row, line, row == 0 && t->top == 0 ?
				   STEEL_ATTR(STEEL_BLUE, STEEL_WHITE) : A_WIN);
		row++;
		p += n;
		if (*p == '\n')
			p++;
	}
}

static bool tl_key(struct win *w, int key, u8 mods)
{
	struct tl *t = DESK_PRIV(w, struct tl);
	int maxtop = max(t->nlines - w->client.h, 0);

	(void)mods;
	switch (key) {
	case KEY_UP:
		t->top = max(t->top - 1, 0);
		return true;
	case KEY_DOWN:
		t->top = min(t->top + 1, maxtop);
		return true;
	case KEY_PGUP:
		t->top = max(t->top - w->client.h, 0);
		return true;
	case KEY_PGDN:
		t->top = min(t->top + w->client.h, maxtop);
		return true;
	case KEY_F(5):
		refresh(t);
		return true;
	default:
		return false;
	}
}

static void tl_tick(struct win *w)
{
	refresh(DESK_PRIV(w, struct tl));
}

static void tl_command(struct win *w, int id)
{
	if (id == TL_REFRESH)
		refresh(DESK_PRIV(w, struct tl));
}

const struct app app_tasks = {
	.name = "tasks",
	.title = "Task List",
	.icon = { "\xDB\xDF\xDF\xDB\xDF\xDF", "\xDB\xDC\xDC\xDB\xDC\xDC" },
	.icon_attr = { STEEL_ATTR(STEEL_BLUE, STEEL_WHITE),
		       STEEL_ATTR(STEEL_BLUE, STEEL_WHITE) },
	.cw = 72, .ch = 16,
	.flags = APP_SINGLE | APP_RESIZABLE,
	.tick_ms = 1000,
	.menus = menus, .nmenus = ARRAY_SIZE(menus),
	.open = tl_open,
	.paint = tl_paint,
	.key = tl_key,
	.tick = tl_tick,
	.command = tl_command,
};
