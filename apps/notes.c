// SPDX-License-Identifier: GPL-2.0-only
/*
 * desk: Notes. A plain text editor with word wrapping. Files are read
 * and written through libq, so it edits whatever the VFS holds.
 *
 * The text lives in one array; a character's place on screen is found by
 * walking the text from the start, which is plenty fast for 7000 bytes.
 */
#include <hix/kernel.h>
#include <qubix/libq.h>
#include <desk/desk.h>

#define TEXT_MAX	6800
#define PATH_LEN	60

struct np {
	char text[TEXT_MAX];
	int len;
	int cur;			/* insertion point, 0 .. len */
	int top;			/* first visible visual row */
	int goal;			/* wanted column for Up/Down */
	bool modified;
	char path[PATH_LEN];
};

enum { NP_NEW = 1, NP_OPEN, NP_SAVE, NP_SAVEAS, NP_DATE };

static const struct menu_item file_items[] = {
	{ "New", NP_NEW, NULL },
	{ "Open...", NP_OPEN, NULL },
	{ "Save", NP_SAVE, NULL },
	{ "Save As...", NP_SAVEAS, NULL },
	{ "", 0, NULL },
	{ "Exit", CMD_CLOSE, "Alt+F4" },
};
static const struct menu_item edit_items[] = {
	{ "Time/Date", NP_DATE, "F5" },
};
static const struct menu menus[] = {
	{ "File", file_items, ARRAY_SIZE(file_items) },
	{ "Edit", edit_items, ARRAY_SIZE(edit_items) },
	DESK_HELP_MENU,
};

/* ---- layout ---------------------------------------------------------- */

/* screen row and column of text offset @idx for a window @cw wide */
static void pos_of(const struct np *n, int cw, int idx, int *row, int *col)
{
	int i, r = 0, c = 0;

	for (i = 0; i < idx; i++) {
		if (n->text[i] == '\n') {
			r++;
			c = 0;
		} else if (++c >= cw) {
			r++;
			c = 0;
		}
	}
	*row = r;
	*col = c;
}

/* the offset at (@row, @col), clamped to the end of that row */
static int index_at(const struct np *n, int cw, int row, int col)
{
	int i, r = 0, c = 0, best = -1;

	for (i = 0;; i++) {
		/* insertion point i sits at (r, c) */
		if (r == row) {
			if (c > col)
				break;
			best = i;
		} else if (r > row) {
			break;
		}
		if (i == n->len)
			break;
		if (n->text[i] == '\n' || ++c >= cw) {
			r++;
			c = 0;
		}
	}
	return best < 0 ? n->len : best;
}

static int total_rows(const struct np *n, int cw)
{
	int r, c;

	pos_of(n, cw, n->len, &r, &c);
	return r + 1;
}

/* ---- editing --------------------------------------------------------- */

static void set_title(struct win *w)
{
	struct np *n = DESK_PRIV(w, struct np);
	char t[40];
	const char *name = n->path;
	const char *slash = strrchr(n->path, '/');

	if (slash)
		name = slash + 1;
	snprintf(t, sizeof(t), "Notes - %s", *n->path ? name : "(Untitled)");
	desk_set_title(w, t);
}

static void insert(struct np *n, char ch)
{
	if (n->len >= TEXT_MAX - 1)
		return;
	memmove(n->text + n->cur + 1, n->text + n->cur, (size_t)(n->len - n->cur));
	n->text[n->cur++] = ch;
	n->len++;
	n->modified = true;
}

static void erase(struct np *n, int at)
{
	if (at < 0 || at >= n->len)
		return;
	memmove(n->text + at, n->text + at + 1, (size_t)(n->len - at - 1));
	n->len--;
	n->modified = true;
	if (n->cur > at)
		n->cur--;
}

static void clear_text(struct win *w)
{
	struct np *n = DESK_PRIV(w, struct np);

	n->len = n->cur = n->top = n->goal = 0;
	n->modified = false;
	n->path[0] = '\0';
	set_title(w);
}

static bool load(struct win *w, const char *path)
{
	struct np *n = DESK_PRIV(w, struct np);
	long fd = q_open(path, O_RDONLY), got;
	int len = 0;

	if (fd < 0)
		return false;
	while (len < TEXT_MAX - 1 &&
	       (got = q_read((int)fd, n->text + len, (size_t)(TEXT_MAX - 1 - len))) > 0)
		len += (int)got;
	q_close((int)fd);

	/* drop carriage returns, turn tabs into spaces */
	{
		int i, o = 0;

		for (i = 0; i < len; i++) {
			if (n->text[i] == '\r')
				continue;
			n->text[o++] = n->text[i] == '\t' ? ' ' : n->text[i];
		}
		len = o;
	}
	n->len = len;
	n->cur = n->top = n->goal = 0;
	n->modified = false;
	strlcpy(n->path, path, sizeof(n->path));
	set_title(w);
	return true;
}

static bool save(struct win *w, const char *path)
{
	struct np *n = DESK_PRIV(w, struct np);
	long fd = q_open(path, O_WRONLY | O_CREAT | O_TRUNC), put;

	if (fd < 0)
		return false;
	put = q_write((int)fd, n->text, (size_t)n->len);
	q_close((int)fd);
	if (put != n->len)
		return false;
	strlcpy(n->path, path, sizeof(n->path));
	n->modified = false;
	set_title(w);
	return true;
}

/* ---- dialogs --------------------------------------------------------- */

static void do_new(struct win *w)
{
	clear_text(w);
}

static void ask_discard(struct win *w, void (*then)(struct win *w))
{
	if (!DESK_PRIV(w, struct np)->modified)
		then(w);
	else
		desk_confirm(w, "Notes",
			     "The text has changed.\nDiscard the changes?", then);
}

static void open_done(struct win *w, const char *name)
{
	if (*name && !load(w, name))
		desk_msgbox("Notes", "Cannot open that file.");
}

static void do_open(struct win *w)
{
	desk_prompt(w, "Open", "File name:", "/", open_done);
}

static void saveas_done(struct win *w, const char *name)
{
	if (*name && !save(w, name))
		desk_msgbox("Notes", "Cannot save that file.\nIs the folder there?");
}

static void do_saveas(struct win *w)
{
	struct np *n = DESK_PRIV(w, struct np);

	desk_prompt(w, "Save As", "File name:", *n->path ? n->path : "/tmp/note.txt",
		    saveas_done);
}

static void do_save(struct win *w)
{
	struct np *n = DESK_PRIV(w, struct np);

	if (!*n->path)
		do_saveas(w);
	else if (!save(w, n->path))
		desk_msgbox("Notes", "Cannot save the file.");
}

static void close_ok(struct win *w)
{
	desk_close(w);
}

static bool np_close_request(struct win *w)
{
	if (!DESK_PRIV(w, struct np)->modified)
		return true;
	desk_confirm(w, "Notes", "The text has changed.\nClose without saving?",
		     close_ok);
	return false;
}

static void np_command(struct win *w, int id)
{
	struct np *n = DESK_PRIV(w, struct np);

	switch (id) {
	case NP_NEW:
		ask_discard(w, do_new);
		break;
	case NP_OPEN:
		ask_discard(w, do_open);
		break;
	case NP_SAVE:
		do_save(w);
		break;
	case NP_SAVEAS:
		do_saveas(w);
		break;
	case NP_DATE: {
		long t = q_time(), s = t % 86400;
		char buf[24];
		int i;

		snprintf(buf, sizeof(buf), "%02ld:%02ld ", s / 3600, s / 60 % 60);
		for (i = 0; buf[i]; i++)
			insert(n, buf[i]);
		break;
	}
	default:
		break;
	}
}

/* ---- the window ------------------------------------------------------ */

static void np_open(struct win *w, const char *arg)
{
	clear_text(w);
	if (arg && *arg && !load(w, arg)) {
		/* a name that does not exist yet: start a new file with it */
		strlcpy(DESK_PRIV(w, struct np)->path, arg, PATH_LEN);
		set_title(w);
	}
}

static void np_paint(struct win *w, struct steel_surface *c)
{
	struct np *n = DESK_PRIV(w, struct np);
	int row, col, i, r = 0, cc = 0;

	pos_of(n, c->w, n->cur, &row, &col);
	if (row < n->top)
		n->top = row;
	if (row >= n->top + c->h)
		n->top = row - c->h + 1;

	for (i = 0; i < n->len; i++) {
		if (r >= n->top + c->h)
			break;
		if (n->text[i] == '\n') {
			r++;
			cc = 0;
			continue;
		}
		if (r >= n->top)
			steel_put(c, cc, r - n->top, (u8)n->text[i], A_WIN);
		if (++cc >= c->w) {
			r++;
			cc = 0;
		}
	}
	if (desk_is_active(w) && col < c->w && row - n->top < c->h) {
		steel_cell_t cell = steel_get(c, col, row - n->top);

		steel_put(c, col, row - n->top, STEEL_CH(cell) ? STEEL_CH(cell) : ' ',
			  STEEL_ATTR(STEEL_WHITE, STEEL_BLACK));
	}
}

static void move_vertical(struct win *w, int rows)
{
	struct np *n = DESK_PRIV(w, struct np);
	int cw = w->client.w, row, col, target;

	pos_of(n, cw, n->cur, &row, &col);
	target = row + rows;
	if (target < 0)
		target = 0;
	if (target >= total_rows(n, cw))
		target = total_rows(n, cw) - 1;
	n->cur = index_at(n, cw, target, n->goal);
}

static bool np_key(struct win *w, int key, u8 mods)
{
	struct np *n = DESK_PRIV(w, struct np);
	int cw = w->client.w, row, col;
	bool vertical = false;

	(void)mods;
	switch (key) {
	case KEY_LEFT:
		n->cur = max(n->cur - 1, 0);
		break;
	case KEY_RIGHT:
		n->cur = min(n->cur + 1, n->len);
		break;
	case KEY_UP:
		move_vertical(w, -1);
		vertical = true;
		break;
	case KEY_DOWN:
		move_vertical(w, 1);
		vertical = true;
		break;
	case KEY_PGUP:
		move_vertical(w, -w->client.h);
		vertical = true;
		break;
	case KEY_PGDN:
		move_vertical(w, w->client.h);
		vertical = true;
		break;
	case KEY_HOME:
		pos_of(n, cw, n->cur, &row, &col);
		n->cur = index_at(n, cw, row, 0);
		break;
	case KEY_END:
		pos_of(n, cw, n->cur, &row, &col);
		n->cur = index_at(n, cw, row, cw);
		break;
	case KEY_DEL:
		erase(n, n->cur);
		break;
	case 8:
	case 127:
		erase(n, n->cur - 1);
		break;
	case '\n':
		insert(n, '\n');
		break;
	case '\t':
		insert(n, ' ');
		insert(n, ' ');
		insert(n, ' ');
		insert(n, ' ');
		break;
	case KEY_F(5):
		np_command(w, NP_DATE);
		break;
	default:
		if (key < 0x20 || key >= 0x7f)
			return false;
		insert(n, (char)key);
		break;
	}
	if (!vertical) {
		pos_of(n, cw, n->cur, &row, &col);
		n->goal = col;
	}
	return true;
}

static void np_mouse(struct win *w, int ev, int x, int y, int buttons)
{
	struct np *n = DESK_PRIV(w, struct np);

	if (ev != MEV_DOWN || !(buttons & MOUSE_LEFT))
		return;
	n->cur = index_at(n, w->client.w, n->top + y, x);
	n->goal = x;
}

const struct app app_notes = {
	.name = "notes",
	.title = "Notes",
	.icon = { "\xDA\xC4\xC4\xC4\xC4\xBF", "\xB3 \xF0\xF0\xF0 \xB3" },
	.icon_attr = { STEEL_ATTR(STEEL_BLACK, STEEL_WHITE),
		       STEEL_ATTR(STEEL_BLUE, STEEL_WHITE) },
	.cw = 60, .ch = 16,
	.flags = APP_RESIZABLE,
	.menus = menus, .nmenus = ARRAY_SIZE(menus),
	.open = np_open,
	.close_request = np_close_request,
	.paint = np_paint,
	.key = np_key,
	.mouse = np_mouse,
	.command = np_command,
};
