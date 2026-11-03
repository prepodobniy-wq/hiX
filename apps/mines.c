// SPDX-License-Identifier: GPL-2.0-only
/*
 * desk: Minefield. Left click opens, right click (or F) flags, the face
 * starts a new game. Cells are two characters wide so they come out square.
 */
#include <hix/kernel.h>
#include <desk/desk.h>
#include <desk/games.h>

/* ---- the rules ------------------------------------------------------- */

static int idx(const struct mines *m, int x, int y)
{
	return y * m->w + x;
}

static bool inside(const struct mines *m, int x, int y)
{
	return x >= 0 && y >= 0 && x < m->w && y < m->h;
}

void mines_init(struct mines *m, int w, int h, int nmines)
{
	memset(m, 0, sizeof(*m));
	m->w = max(1, min(w, MINES_MAX_W));
	m->h = max(1, min(h, MINES_MAX_H));
	m->nmines = max(1, min(nmines, m->w * m->h - 9));
	m->status = MS_READY;
}

static void place_mines(struct mines *m, int sx, int sy)
{
	int placed = 0, x, y, dx, dy;

	while (placed < m->nmines) {
		int i;

		x = (int)(desk_rand() % (u32)m->w);
		y = (int)(desk_rand() % (u32)m->h);
		i = idx(m, x, y);
		if (m->mine[i] || (abs(x - sx) <= 1 && abs(y - sy) <= 1))
			continue;
		m->mine[i] = 1;
		placed++;
	}
	for (y = 0; y < m->h; y++) {
		for (x = 0; x < m->w; x++) {
			int n = 0;

			for (dy = -1; dy <= 1; dy++)
				for (dx = -1; dx <= 1; dx++)
					if (inside(m, x + dx, y + dy) &&
					    m->mine[idx(m, x + dx, y + dy)])
						n++;
			m->adj[idx(m, x, y)] = (u8)n;
		}
	}
}

/* open a cell and, when it has no neighbouring mines, the area around it */
static void flood(struct mines *m, int x, int y)
{
	static u16 stack[MINES_MAX_W * MINES_MAX_H];
	int sp = 0;

	stack[sp++] = (u16)idx(m, x, y);
	m->state[idx(m, x, y)] = MC_OPEN;
	m->opened++;
	while (sp) {
		int i = stack[--sp], cx = i % m->w, cy = i / m->w, dx, dy;

		if (m->adj[i])
			continue;
		for (dy = -1; dy <= 1; dy++) {
			for (dx = -1; dx <= 1; dx++) {
				int nx = cx + dx, ny = cy + dy, j;

				if (!inside(m, nx, ny))
					continue;
				j = idx(m, nx, ny);
				if (m->state[j] != MC_HIDDEN || m->mine[j])
					continue;
				m->state[j] = MC_OPEN;
				m->opened++;
				stack[sp++] = (u16)j;
			}
		}
	}
}

void mines_reveal(struct mines *m, int x, int y)
{
	int i;

	if (!inside(m, x, y) || m->status >= MS_WON)
		return;
	i = idx(m, x, y);
	if (m->state[i] != MC_HIDDEN)
		return;
	if (m->status == MS_READY) {
		place_mines(m, x, y);
		m->status = MS_PLAYING;
	}
	if (m->mine[i]) {
		m->state[i] = MC_OPEN;
		m->hit_x = x;
		m->hit_y = y;
		m->status = MS_LOST;
		return;
	}
	flood(m, x, y);
	if (m->opened == m->w * m->h - m->nmines) {
		int k;

		m->status = MS_WON;
		for (k = 0; k < m->w * m->h; k++)	/* flag what is left */
			if (m->mine[k] && m->state[k] != MC_FLAG) {
				m->state[k] = MC_FLAG;
				m->flags++;
			}
	}
}

void mines_flag(struct mines *m, int x, int y)
{
	int i;

	if (!inside(m, x, y) || m->status >= MS_WON)
		return;
	i = idx(m, x, y);
	if (m->state[i] == MC_HIDDEN) {
		m->state[i] = MC_FLAG;
		m->flags++;
	} else if (m->state[i] == MC_FLAG) {
		m->state[i] = MC_HIDDEN;
		m->flags--;
	}
}

/* ---- the window ------------------------------------------------------ */

enum { MN_NEW = 1, MN_BEGIN, MN_INTER, MN_EXPERT };

struct mw {
	struct mines g;
	int level;		/* MN_BEGIN .. MN_EXPERT */
	int cx, cy;		/* keyboard cursor */
	int seconds;
};

static const struct menu_item game_items[] = {
	{ "New", MN_NEW, "F2" },
	{ "", 0, NULL },
	{ "Beginner", MN_BEGIN, NULL },
	{ "Intermediate", MN_INTER, NULL },
	{ "Expert", MN_EXPERT, NULL },
	{ "", 0, NULL },
	{ "Exit", CMD_CLOSE, "Alt+F4" },
};
static const struct menu menus[] = {
	{ "Game", game_items, ARRAY_SIZE(game_items) },
	DESK_HELP_MENU,
};

static void level_size(int level, int *w, int *h, int *n)
{
	switch (level) {
	case MN_INTER:
		*w = 16, *h = 16, *n = 40;
		break;
	case MN_EXPERT:
		*w = 30, *h = 16, *n = 99;
		break;
	default:
		*w = 9, *h = 9, *n = 10;
		break;
	}
}

static void new_game(struct win *w, int level)
{
	struct mw *mw = DESK_PRIV(w, struct mw);
	int bw, bh, n;

	level_size(level, &bw, &bh, &n);
	mw->level = level;
	mines_init(&mw->g, bw, bh, n);
	mw->cx = bw / 2;
	mw->cy = bh / 2;
	mw->seconds = 0;
	desk_set_client_size(w, max(bw * 2, 18), bh + 2);
}

static void mines_open(struct win *w, const char *arg)
{
	(void)arg;
	new_game(w, MN_BEGIN);
}

static int board_x(const struct win *w)
{
	const struct mw *mw = (const struct mw *)(const void *)w->priv;

	return (w->client.w - mw->g.w * 2) / 2;
}

static const char *face(const struct mines *m)
{
	switch (m->status) {
	case MS_WON:
		return "B)";
	case MS_LOST:
		return "X(";
	default:
		return ":)";
	}
}

static void mines_paint(struct win *w, struct steel_surface *c)
{
	static const u8 numcol[9] = {
		STEEL_BLACK, STEEL_BLUE, STEEL_GREEN, STEEL_RED, STEEL_MAGENTA,
		STEEL_BROWN, STEEL_CYAN, STEEL_BLACK, STEEL_DGRAY,
	};
	struct mw *mw = DESK_PRIV(w, struct mw);
	struct mines *m = &mw->g;
	const u8 gray = STEEL_ATTR(STEEL_BLACK, STEEL_LGRAY);
	char buf[8];
	int ox = board_x(w), x, y;

	steel_fill(c, ' ', gray);
	snprintf(buf, sizeof(buf), "%03d", max(m->nmines - m->flags, 0));
	steel_puts(c, 1, 0, buf, STEEL_ATTR(STEEL_LRED, STEEL_BLACK));
	snprintf(buf, sizeof(buf), "%03d", min(mw->seconds, 999));
	steel_puts(c, c->w - 4, 0, buf, STEEL_ATTR(STEEL_LRED, STEEL_BLACK));
	steel_puts(c, c->w / 2 - 1, 0, face(m), STEEL_ATTR(STEEL_BLACK, STEEL_YELLOW));

	for (y = 0; y < m->h; y++) {
		for (x = 0; x < m->w; x++) {
			int i = y * m->w + x;
			u8 st = m->state[i];
			u8 attr;
			char c1 = ' ', c2 = ' ';

			if (st == MC_HIDDEN) {
				attr = STEEL_ATTR(STEEL_DGRAY, STEEL_LGRAY);
				c1 = c2 = (char)0xB1;
				if (m->status == MS_LOST && m->mine[i]) {
					attr = STEEL_ATTR(STEEL_BLACK, STEEL_LGRAY);
					c1 = (char)0x0F, c2 = ' ';
				}
			} else if (st == MC_FLAG) {
				attr = STEEL_ATTR(STEEL_LRED, STEEL_LGRAY);
				c1 = ' ', c2 = (char)0x10;
				if (m->status == MS_LOST && !m->mine[i]) {
					attr = STEEL_ATTR(STEEL_RED, STEEL_LGRAY);
					c1 = 'X', c2 = ' ';
				}
			} else if (m->mine[i]) {
				attr = STEEL_ATTR(STEEL_BLACK, STEEL_RED);
				c1 = (char)0x0F, c2 = ' ';
			} else {
				attr = STEEL_ATTR(numcol[m->adj[i]], STEEL_WHITE);
				if (m->adj[i])
					c2 = (char)('0' + m->adj[i]);
			}
			if (desk_is_active(w) && x == mw->cx && y == mw->cy &&
			    m->status < MS_WON)
				attr = STEEL_ATTR(STEEL_WHITE, STEEL_BLUE);
			steel_put(c, ox + x * 2, 1 + y, (u8)c1, attr);
			steel_put(c, ox + x * 2 + 1, 1 + y, (u8)c2, attr);
		}
	}
	if (m->status == MS_WON)
		steel_puts(c, 1, c->h - 1, " You win! ",
			   STEEL_ATTR(STEEL_WHITE, STEEL_GREEN));
	else if (m->status == MS_LOST)
		steel_puts(c, 1, c->h - 1, " Boom! F2 = new game ",
			   STEEL_ATTR(STEEL_WHITE, STEEL_RED));
}

static bool mines_key(struct win *w, int key, u8 mods)
{
	struct mw *mw = DESK_PRIV(w, struct mw);

	(void)mods;
	switch (key) {
	case KEY_LEFT:
		mw->cx = max(mw->cx - 1, 0);
		return true;
	case KEY_RIGHT:
		mw->cx = min(mw->cx + 1, mw->g.w - 1);
		return true;
	case KEY_UP:
		mw->cy = max(mw->cy - 1, 0);
		return true;
	case KEY_DOWN:
		mw->cy = min(mw->cy + 1, mw->g.h - 1);
		return true;
	case ' ':
	case '\n':
		mines_reveal(&mw->g, mw->cx, mw->cy);
		return true;
	case 'f':
	case 'F':
		mines_flag(&mw->g, mw->cx, mw->cy);
		return true;
	case KEY_F(2):
		new_game(w, mw->level);
		return true;
	default:
		return false;
	}
}

static void mines_mouse(struct win *w, int ev, int x, int y, int buttons)
{
	struct mw *mw = DESK_PRIV(w, struct mw);
	int bx = (x - board_x(w)) / 2, by = y - 1;

	if (ev != MEV_DOWN)
		return;
	if (y == 0) {
		if ((buttons & MOUSE_LEFT) && abs(x - w->client.w / 2) <= 1)
			new_game(w, mw->level);
		return;
	}
	if (x < board_x(w) || bx >= mw->g.w || by < 0 || by >= mw->g.h)
		return;
	mw->cx = bx;
	mw->cy = by;
	if (buttons == MOUSE_RIGHT)
		mines_flag(&mw->g, bx, by);
	else if (buttons & MOUSE_LEFT)
		mines_reveal(&mw->g, bx, by);
}

static void mines_tick(struct win *w)
{
	struct mw *mw = DESK_PRIV(w, struct mw);

	if (mw->g.status == MS_PLAYING && mw->seconds < 999)
		mw->seconds++;
}

static void mines_command(struct win *w, int id)
{
	struct mw *mw = DESK_PRIV(w, struct mw);

	if (id == MN_NEW)
		new_game(w, mw->level);
	else if (id >= MN_BEGIN && id <= MN_EXPERT)
		new_game(w, id);
}

const struct app app_mines = {
	.name = "mines",
	.title = "Minefield",
	.icon = { "\xB1\xB1\x0F\xB1\xB1\xB1", "\xB1 1 \xB1\x10" },
	.icon_attr = { STEEL_ATTR(STEEL_DGRAY, STEEL_WHITE),
		       STEEL_ATTR(STEEL_RED, STEEL_WHITE) },
	.cw = 18, .ch = 11,
	.flags = APP_SINGLE,
	.tick_ms = 1000,
	.menus = menus, .nmenus = ARRAY_SIZE(menus),
	.open = mines_open,
	.paint = mines_paint,
	.key = mines_key,
	.mouse = mines_mouse,
	.tick = mines_tick,
	.command = mines_command,
};
