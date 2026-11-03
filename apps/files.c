// SPDX-License-Identifier: GPL-2.0-only
/*
 * desk: Files. Browses the in-memory VFS: Enter or a double click
 * opens a folder or edits a file in Notes, Backspace goes up, F5
 * refreshes. File/Delete and File/New Folder ask first.
 */
#include <hix/kernel.h>
#include <qubix/libq.h>
#include <desk/desk.h>

#define MAX_ENTRIES	64
#define PATH_LEN	64

struct fm {
	struct dirent_rec e[MAX_ENTRIES];
	int n;
	int sel, top;
	char path[PATH_LEN];
	bool failed;
};

enum { FM_OPEN = 1, FM_MKDIR, FM_DELETE, FM_REFRESH, FM_UP };

static const struct menu_item file_items[] = {
	{ "Open", FM_OPEN, "Enter" },
	{ "New Folder...", FM_MKDIR, NULL },
	{ "Delete...", FM_DELETE, "Del" },
	{ "", 0, NULL },
	{ "Exit", CMD_CLOSE, "Alt+F4" },
};
static const struct menu_item view_items[] = {
	{ "Parent Folder", FM_UP, "Bksp" },
	{ "Refresh", FM_REFRESH, "F5" },
};
static const struct menu menus[] = {
	{ "File", file_items, ARRAY_SIZE(file_items) },
	{ "View", view_items, ARRAY_SIZE(view_items) },
	DESK_HELP_MENU,
};

static void join(const struct fm *f, const char *name, char *out, size_t size)
{
	if (!strcmp(f->path, "/"))
		snprintf(out, size, "/%s", name);
	else
		snprintf(out, size, "%s/%s", f->path, name);
}

static void reload(struct fm *f)
{
	long n = q_readdir(f->path, f->e, MAX_ENTRIES);

	f->failed = n < 0;
	f->n = n < 0 ? 0 : (int)n;
	f->sel = min(f->sel, max(f->n - 1, 0));
	f->top = min(f->top, f->sel);
}

static void go(struct fm *f, const char *path)
{
	strlcpy(f->path, path, sizeof(f->path));
	f->sel = f->top = 0;
	reload(f);
}

static void go_up(struct fm *f)
{
	char *slash = strrchr(f->path, '/');

	if (!slash || slash == f->path) {
		go(f, "/");
		return;
	}
	*slash = '\0';
	go(f, f->path);
}

static void fm_open(struct win *w, const char *arg)
{
	struct fm *f = DESK_PRIV(w, struct fm);

	go(f, arg && *arg ? arg : "/");
}

static void open_selected(struct win *w)
{
	struct fm *f = DESK_PRIV(w, struct fm);
	char full[PATH_LEN + 24];

	if (f->sel >= f->n)
		return;
	join(f, f->e[f->sel].name, full, sizeof(full));
	if (f->e[f->sel].type == VT_DIR)
		go(f, full);
	else if (f->e[f->sel].type == VT_REG)
		desk_launch(&app_notes, full);
}

static void mkdir_done(struct win *w, const char *name)
{
	struct fm *f = DESK_PRIV(w, struct fm);
	char full[PATH_LEN + 48];

	if (!*name)
		return;
	join(f, name, full, sizeof(full));
	if (q_mkdir(full))
		desk_msgbox("Files", "Cannot create that folder.");
	reload(f);
}

static void delete_ok(struct win *w)
{
	struct fm *f = DESK_PRIV(w, struct fm);
	char full[PATH_LEN + 24];

	if (f->sel >= f->n)
		return;
	join(f, f->e[f->sel].name, full, sizeof(full));
	if (q_unlink(full))
		desk_msgbox("Files", "Cannot delete that.\n"
			    "Folders must be empty; devices stay.");
	reload(f);
}

static void ask_delete(struct win *w)
{
	struct fm *f = DESK_PRIV(w, struct fm);
	char text[80];

	if (f->sel >= f->n)
		return;
	snprintf(text, sizeof(text), "Delete %s?", f->e[f->sel].name);
	desk_confirm(w, "Files", text, delete_ok);
}

static void fm_command(struct win *w, int id)
{
	struct fm *f = DESK_PRIV(w, struct fm);

	switch (id) {
	case FM_OPEN:
		open_selected(w);
		break;
	case FM_MKDIR:
		desk_prompt(w, "New Folder", "Folder name:", "", mkdir_done);
		break;
	case FM_DELETE:
		ask_delete(w);
		break;
	case FM_REFRESH:
		reload(f);
		break;
	case FM_UP:
		go_up(f);
		break;
	}
}

static void fm_paint(struct win *w, struct steel_surface *c)
{
	struct fm *f = DESK_PRIV(w, struct fm);
	const u8 bar = STEEL_ATTR(STEEL_BLACK, STEEL_LGRAY);
	int rows = c->h - 2, i;
	char info[24];

	if (f->sel < f->top)
		f->top = f->sel;
	if (f->sel >= f->top + rows)
		f->top = f->sel - rows + 1;

	steel_rect(c, 0, 0, c->w, 1, ' ', bar);
	steel_puts(c, 1, 0, f->path, bar);
	if (f->failed)
		steel_puts(c, 1, 2, "This folder cannot be read.", A_WIN);
	for (i = 0; i < rows && f->top + i < f->n; i++) {
		const struct dirent_rec *e = &f->e[f->top + i];
		bool sel = f->top + i == f->sel;
		u8 attr = sel ? A_SEL : A_WIN;

		steel_rect(c, 0, 1 + i, c->w, 1, ' ', attr);
		steel_put(c, 1, 1 + i, e->type == VT_DIR ? 0x10 :
			  e->type == VT_CHR ? 0x0F : 0xFE, attr);
		steel_puts(c, 3, 1 + i, e->name, attr);
		if (e->type == VT_DIR)
			snprintf(info, sizeof(info), "<DIR>");
		else if (e->type == VT_CHR)
			snprintf(info, sizeof(info), "<DEV>");
		else
			snprintf(info, sizeof(info), "%u", e->size);
		steel_puts(c, c->w - 2 - (int)strlen(info), 1 + i, info, attr);
	}
	snprintf(info, sizeof(info), " %d item%s", f->n, f->n == 1 ? "" : "s");
	steel_rect(c, 0, c->h - 1, c->w, 1, ' ', bar);
	steel_puts(c, 0, c->h - 1, info, bar);
}

static bool fm_key(struct win *w, int key, u8 mods)
{
	struct fm *f = DESK_PRIV(w, struct fm);
	int rows = max(w->client.h - 2, 1);

	(void)mods;
	switch (key) {
	case KEY_UP:
		f->sel = max(f->sel - 1, 0);
		return true;
	case KEY_DOWN:
		f->sel = min(f->sel + 1, max(f->n - 1, 0));
		return true;
	case KEY_PGUP:
		f->sel = max(f->sel - rows, 0);
		return true;
	case KEY_PGDN:
		f->sel = min(f->sel + rows, max(f->n - 1, 0));
		return true;
	case KEY_HOME:
		f->sel = 0;
		return true;
	case KEY_END:
		f->sel = max(f->n - 1, 0);
		return true;
	case '\n':
		open_selected(w);
		return true;
	case 8:
	case 127:
		go_up(f);
		return true;
	case KEY_DEL:
		ask_delete(w);
		return true;
	case KEY_F(5):
		reload(f);
		return true;
	default:
		return false;
	}
}

static void fm_mouse(struct win *w, int ev, int x, int y, int buttons)
{
	struct fm *f = DESK_PRIV(w, struct fm);
	int i = f->top + y - 1;

	(void)x;
	if ((ev != MEV_DOWN && ev != MEV_DBL) || !(buttons & MOUSE_LEFT))
		return;
	if (y < 1 || y > w->client.h - 2 || i >= f->n)
		return;
	f->sel = i;
	if (ev == MEV_DBL)
		open_selected(w);
}

const struct app app_files = {
	.name = "files",
	.title = "Files",
	.icon = { "\xDC\xDC\xDC\xDC\xDB\xDB", "\xDB\xDB\xDB\xDB\xDB\xDB" },
	.icon_attr = { STEEL_ATTR(STEEL_BROWN, STEEL_WHITE),
		       STEEL_ATTR(STEEL_BROWN, STEEL_WHITE) },
	.cw = 50, .ch = 16,
	.flags = APP_RESIZABLE,
	.menus = menus, .nmenus = ARRAY_SIZE(menus),
	.open = fm_open,
	.paint = fm_paint,
	.key = fm_key,
	.mouse = fm_mouse,
	.command = fm_command,
};
