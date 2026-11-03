// SPDX-License-Identifier: GPL-2.0-only
/*
 * desk: Reversi. You play black against the computer; click a square (or
 * move the cursor and press Enter) to place a disc. F2 starts over.
 */
#include <hix/kernel.h>
#include <desk/desk.h>
#include <desk/games.h>

/* ---- the rules ------------------------------------------------------- */

static const int dirs[8][2] = {
	{ -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 },
	{ -1, 1 }, { 0, 1 }, { 1, 1 },
};

static int other(int who)
{
	return who == RV_BLACK ? RV_WHITE : RV_BLACK;
}

void reversi_init(struct reversi *r)
{
	memset(r, 0, sizeof(*r));
	r->b[3][3] = r->b[4][4] = RV_WHITE;
	r->b[3][4] = r->b[4][3] = RV_BLACK;
	r->turn = RV_BLACK;
}

/* number of discs @who would flip by playing at (x, y); 0 = illegal */
int reversi_flips(const struct reversi *r, int x, int y, int who)
{
	int d, total = 0;

	if (x < 0 || y < 0 || x > 7 || y > 7 || r->b[y][x] != RV_EMPTY)
		return 0;
	for (d = 0; d < 8; d++) {
		int cx = x + dirs[d][0], cy = y + dirs[d][1], n = 0;

		while (cx >= 0 && cy >= 0 && cx < 8 && cy < 8 &&
		       r->b[cy][cx] == other(who)) {
			cx += dirs[d][0];
			cy += dirs[d][1];
			n++;
		}
		if (n && cx >= 0 && cy >= 0 && cx < 8 && cy < 8 &&
		    r->b[cy][cx] == who)
			total += n;
	}
	return total;
}

bool reversi_play(struct reversi *r, int x, int y)
{
	int d, who = r->turn;

	if (r->over || !reversi_flips(r, x, y, who))
		return false;
	for (d = 0; d < 8; d++) {
		int cx = x + dirs[d][0], cy = y + dirs[d][1], n = 0;

		while (cx >= 0 && cy >= 0 && cx < 8 && cy < 8 &&
		       r->b[cy][cx] == other(who)) {
			cx += dirs[d][0];
			cy += dirs[d][1];
			n++;
		}
		if (n && cx >= 0 && cy >= 0 && cx < 8 && cy < 8 &&
		    r->b[cy][cx] == who) {
			while (n--) {
				cx -= dirs[d][0];
				cy -= dirs[d][1];
				r->b[cy][cx] = (u8)who;
			}
		}
	}
	r->b[y][x] = (u8)who;
	return true;
}

bool reversi_has_move(const struct reversi *r, int who)
{
	int x, y;

	for (y = 0; y < 8; y++)
		for (x = 0; x < 8; x++)
			if (reversi_flips(r, x, y, who))
				return true;
	return false;
}

int reversi_count(const struct reversi *r, int who)
{
	int x, y, n = 0;

	for (y = 0; y < 8; y++)
		for (x = 0; x < 8; x++)
			n += r->b[y][x] == who;
	return n;
}

bool reversi_advance(struct reversi *r)
{
	int next = other(r->turn);

	if (reversi_has_move(r, next)) {
		r->turn = next;
		return false;
	}
	if (!reversi_has_move(r, r->turn))
		r->over = true;
	return !r->over;		/* the same side moves again: a pass */
}

/* corners are gold, the squares next to them are traps */
static const s8 weight[8][8] = {
	{ 100, -20, 10, 5, 5, 10, -20, 100 },
	{ -20, -50, -2, -2, -2, -2, -50, -20 },
	{ 10, -2, 3, 1, 1, 3, -2, 10 },
	{ 5, -2, 1, 0, 0, 1, -2, 5 },
	{ 5, -2, 1, 0, 0, 1, -2, 5 },
	{ 10, -2, 3, 1, 1, 3, -2, 10 },
	{ -20, -50, -2, -2, -2, -2, -50, -20 },
	{ 100, -20, 10, 5, 5, 10, -20, 100 },
};

bool reversi_choose(const struct reversi *r, int *bx, int *by)
{
	int x, y, best = -100000;
	bool found = false;

	for (y = 0; y < 8; y++) {
		for (x = 0; x < 8; x++) {
			int f = reversi_flips(r, x, y, r->turn), score;

			if (!f)
				continue;
			score = weight[y][x] * 4 + f;
			if (score > best) {
				best = score;
				*bx = x;
				*by = y;
				found = true;
			}
		}
	}
	return found;
}

/* ---- the window ------------------------------------------------------ */

#define CELL_W	4
#define CELL_H	2
#define BX	2		/* board origin in the client area */
#define BY	2

enum { RV_NEW = 1 };

struct rw {
	struct reversi g;
	int cx, cy;
	int mx, my;		/* last move, -1 if none */
	char msg[48];
};

static const struct menu_item game_items[] = {
	{ "New", RV_NEW, "F2" },
	{ "", 0, NULL },
	{ "Exit", CMD_CLOSE, "Alt+F4" },
};
static const struct menu menus[] = {
	{ "Game", game_items, ARRAY_SIZE(game_items) },
	DESK_HELP_MENU,
};

static void set_msg(struct rw *rw)
{
	const struct reversi *g = &rw->g;
	int b = reversi_count(g, RV_BLACK), w = reversi_count(g, RV_WHITE);

	if (g->over) {
		snprintf(rw->msg, sizeof(rw->msg), "%s",
			 b > w ? "You win!" : b < w ? "The computer wins." :
			 "A draw.");
	} else if (g->turn == RV_BLACK) {
		snprintf(rw->msg, sizeof(rw->msg), "Your move.");
	} else {
		snprintf(rw->msg, sizeof(rw->msg), "The computer is thinking...");
	}
}

static void new_game(struct win *w)
{
	struct rw *rw = DESK_PRIV(w, struct rw);

	reversi_init(&rw->g);
	rw->cx = rw->cy = 3;
	rw->mx = rw->my = -1;
	set_msg(rw);
}

static void reversi_open(struct win *w, const char *arg)
{
	(void)arg;
	new_game(w);
}

static void after_move(struct rw *rw)
{
	if (reversi_advance(&rw->g))
		snprintf(rw->msg, sizeof(rw->msg), "%s has to pass.",
			 rw->g.turn == RV_BLACK ? "The computer" : "You");
	else
		set_msg(rw);
}

static void human_move(struct rw *rw, int x, int y)
{
	if (rw->g.turn != RV_BLACK || rw->g.over)
		return;
	if (!reversi_play(&rw->g, x, y)) {
		snprintf(rw->msg, sizeof(rw->msg), "You cannot play there.");
		return;
	}
	rw->mx = x;
	rw->my = y;
	after_move(rw);
}

static void reversi_paint(struct win *w, struct steel_surface *c)
{
	struct rw *rw = DESK_PRIV(w, struct rw);
	const struct reversi *g = &rw->g;
	const u8 gray = STEEL_ATTR(STEEL_BLACK, STEEL_LGRAY);
	char buf[48];
	int x, y;

	steel_fill(c, ' ', gray);
	snprintf(buf, sizeof(buf), "You (black) %2d    Computer (white) %2d",
		 reversi_count(g, RV_BLACK), reversi_count(g, RV_WHITE));
	steel_puts(c, 2, 0, buf, gray);

	for (y = 0; y < 8; y++) {
		for (x = 0; x < 8; x++) {
			int px = BX + x * CELL_W, py = BY + y * CELL_H;
			bool cursor = x == rw->cx && y == rw->cy &&
				      desk_is_active(w);
			u8 bg = cursor ? STEEL_BROWN : STEEL_GREEN;
			u8 who = g->b[y][x];

			steel_rect(c, px, py, CELL_W, CELL_H, ' ',
				   STEEL_ATTR(STEEL_BLACK, bg));
			if (who != RV_EMPTY) {
				u8 fg = who == RV_BLACK ? STEEL_BLACK : STEEL_WHITE;
				u8 attr = STEEL_ATTR(fg, bg);

				steel_puts(c, px + 1, py, "\xDB\xDB", attr);
				steel_puts(c, px + 1, py + 1, "\xDB\xDB", attr);
			} else if (g->turn == RV_BLACK && !g->over &&
				   reversi_flips(g, x, y, RV_BLACK)) {
				steel_puts(c, px + 1, py, "\xF9\xF9",
					   STEEL_ATTR(STEEL_LGREEN, bg));
			}
			if (x == rw->mx && y == rw->my)
				steel_put(c, px, py, '*', STEEL_ATTR(STEEL_YELLOW, bg));
		}
	}
	steel_puts(c, 2, BY + 8 * CELL_H, rw->msg, gray);
}

static bool reversi_key(struct win *w, int key, u8 mods)
{
	struct rw *rw = DESK_PRIV(w, struct rw);

	(void)mods;
	switch (key) {
	case KEY_LEFT:
		rw->cx = max(rw->cx - 1, 0);
		return true;
	case KEY_RIGHT:
		rw->cx = min(rw->cx + 1, 7);
		return true;
	case KEY_UP:
		rw->cy = max(rw->cy - 1, 0);
		return true;
	case KEY_DOWN:
		rw->cy = min(rw->cy + 1, 7);
		return true;
	case '\n':
	case ' ':
		human_move(rw, rw->cx, rw->cy);
		return true;
	case KEY_F(2):
		new_game(w);
		return true;
	default:
		return false;
	}
}

static void reversi_mouse(struct win *w, int ev, int x, int y, int buttons)
{
	struct rw *rw = DESK_PRIV(w, struct rw);
	int bx = (x - BX) / CELL_W, by = (y - BY) / CELL_H;

	if (ev != MEV_DOWN || !(buttons & MOUSE_LEFT) || x < BX || y < BY ||
	    bx > 7 || by > 7)
		return;
	rw->cx = bx;
	rw->cy = by;
	human_move(rw, bx, by);
}

/* the computer answers on a timer so the player can follow the game */
static void reversi_tick(struct win *w)
{
	struct rw *rw = DESK_PRIV(w, struct rw);
	int x, y;

	if (rw->g.over || rw->g.turn != RV_WHITE)
		return;
	if (reversi_choose(&rw->g, &x, &y)) {
		reversi_play(&rw->g, x, y);
		rw->mx = x;
		rw->my = y;
	}
	after_move(rw);
}

static void reversi_command(struct win *w, int id)
{
	if (id == RV_NEW)
		new_game(w);
}

const struct app app_reversi = {
	.name = "reversi",
	.title = "Reversi",
	.icon = { "\xDB\xDB  \xDB\xDB", "  \xDB\xDB  " },
	.icon_attr = { STEEL_ATTR(STEEL_BLACK, STEEL_GREEN),
		       STEEL_ATTR(STEEL_WHITE, STEEL_GREEN) },
	.cw = 36, .ch = 19,
	.flags = APP_SINGLE,
	.tick_ms = 500,
	.menus = menus, .nmenus = ARRAY_SIZE(menus),
	.open = reversi_open,
	.paint = reversi_paint,
	.key = reversi_key,
	.mouse = reversi_mouse,
	.tick = reversi_tick,
	.command = reversi_command,
};
