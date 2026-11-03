// SPDX-License-Identifier: GPL-2.0-only
/*
 * desk: Bricks. Left/right move, up rotates, down drops one row, space
 * drops all the way, P pauses, F2 starts over.
 */
#include <hix/kernel.h>
#include <desk/desk.h>
#include <desk/games.h>

/* ---- the rules ------------------------------------------------------- */

/* 4x4 masks, row 0 in the high nibble: I J L O S T Z, four rotations each */
static const u16 shapes[7][4] = {
	{ 0x0F00, 0x2222, 0x00F0, 0x4444 },
	{ 0x8E00, 0x6440, 0x0E20, 0x44C0 },
	{ 0x2E00, 0x4460, 0x0E80, 0xC440 },
	{ 0x6600, 0x6600, 0x6600, 0x6600 },
	{ 0x6C00, 0x4620, 0x06C0, 0x8C40 },
	{ 0x4E00, 0x4640, 0x0E40, 0x4C40 },
	{ 0xC600, 0x2640, 0x0C60, 0x4C80 },
};

bool bricks_cell(int piece, int rot, int r, int c)
{
	return shapes[piece][rot & 3] & (0x8000 >> (r * 4 + c));
}

static bool fits(const struct bricks *t, int piece, int rot, int px, int py)
{
	int r, c;

	for (r = 0; r < 4; r++) {
		for (c = 0; c < 4; c++) {
			int x = px + c, y = py + r;

			if (!bricks_cell(piece, rot, r, c))
				continue;
			if (x < 0 || x >= BRICKS_W || y >= BRICKS_H)
				return false;
			if (y >= 0 && t->board[y][x])
				return false;
		}
	}
	return true;
}

static void spawn(struct bricks *t)
{
	t->piece = t->next;
	t->next = (int)(desk_rand() % 7);
	t->rot = 0;
	t->px = 3;
	t->py = -1;
	if (!fits(t, t->piece, t->rot, t->px, t->py))
		t->over = true;
}

void bricks_init(struct bricks *t)
{
	memset(t, 0, sizeof(*t));
	t->next = (int)(desk_rand() % 7);
	t->level = 1;
	spawn(t);
}

bool bricks_move(struct bricks *t, int dx)
{
	if (t->over || !fits(t, t->piece, t->rot, t->px + dx, t->py))
		return false;
	t->px += dx;
	return true;
}

bool bricks_rotate(struct bricks *t)
{
	static const int kick[] = { 0, -1, 1, -2, 2 };
	int rot = (t->rot + 1) & 3, i;

	if (t->over)
		return false;
	for (i = 0; i < 5; i++) {		/* try nudging off a wall */
		if (fits(t, t->piece, rot, t->px + kick[i], t->py)) {
			t->rot = rot;
			t->px += kick[i];
			return true;
		}
	}
	return false;
}

static void lock_piece(struct bricks *t)
{
	static const u32 points[5] = { 0, 40, 100, 300, 1200 };
	int r, c, y, cleared = 0;

	for (r = 0; r < 4; r++)
		for (c = 0; c < 4; c++)
			if (bricks_cell(t->piece, t->rot, r, c) &&
			    t->py + r >= 0)
				t->board[t->py + r][t->px + c] =
					(u8)(t->piece + 1);

	for (y = BRICKS_H - 1; y >= 0; y--) {
		bool full = true;

		for (c = 0; c < BRICKS_W; c++)
			full &= t->board[y][c] != 0;
		if (!full)
			continue;
		memmove(&t->board[1], &t->board[0], (size_t)y * BRICKS_W);
		memset(&t->board[0], 0, BRICKS_W);
		cleared++;
		y++;				/* look at this row again */
	}
	t->score += points[cleared] * (u32)t->level;
	t->lines += cleared;
	t->level = 1 + t->lines / 10;
	spawn(t);
}

bool bricks_step(struct bricks *t)
{
	if (t->over)
		return false;
	if (fits(t, t->piece, t->rot, t->px, t->py + 1)) {
		t->py++;
		return false;
	}
	lock_piece(t);
	return true;
}

void bricks_drop(struct bricks *t)
{
	int rows = 0;

	if (t->over)
		return;
	while (fits(t, t->piece, t->rot, t->px, t->py + 1)) {
		t->py++;
		rows++;
	}
	t->score += (u32)rows * 2;
	lock_piece(t);
}

int bricks_interval(const struct bricks *t)
{
	return max(80, 800 - (t->level - 1) * 70);
}

/* ---- the window ------------------------------------------------------ */

enum { TT_NEW = 1, TT_PAUSE };

struct tw {
	struct bricks g;
	bool paused;
	int acc;
	u32 best;
};

static const struct menu_item game_items[] = {
	{ "New", TT_NEW, "F2" },
	{ "Pause", TT_PAUSE, "P" },
	{ "", 0, NULL },
	{ "Exit", CMD_CLOSE, "Alt+F4" },
};
static const struct menu menus[] = {
	{ "Game", game_items, ARRAY_SIZE(game_items) },
	DESK_HELP_MENU,
};

static const u8 piece_color[7] = {
	STEEL_LCYAN, STEEL_LBLUE, STEEL_BROWN, STEEL_YELLOW, STEEL_LGREEN,
	STEEL_LMAGENTA, STEEL_LRED,
};

static void new_game(struct win *w)
{
	struct tw *tw = DESK_PRIV(w, struct tw);

	if (tw->g.score > tw->best)
		tw->best = tw->g.score;
	bricks_init(&tw->g);
	tw->paused = false;
	tw->acc = 0;
}

static void bricks_open(struct win *w, const char *arg)
{
	(void)arg;
	new_game(w);
}

static void block(struct steel_surface *c, int x, int y, int color)
{
	u8 attr = STEEL_ATTR(STEEL_WHITE, color);

	steel_put(c, x, y, '[', attr);
	steel_put(c, x + 1, y, ']', attr);
}

static void bricks_paint(struct win *w, struct steel_surface *c)
{
	struct tw *tw = DESK_PRIV(w, struct tw);
	const struct bricks *t = &tw->g;
	const u8 well = STEEL_ATTR(STEEL_DGRAY, STEEL_BLACK);
	const u8 panel = STEEL_ATTR(STEEL_BLACK, STEEL_LGRAY);
	char buf[24];
	int x, y, r, k;

	steel_fill(c, ' ', panel);
	for (y = 0; y < BRICKS_H; y++) {
		for (x = 0; x < BRICKS_W; x++) {
			if (t->board[y][x]) {
				block(c, x * 2, y, piece_color[t->board[y][x] - 1]);
			} else {
				steel_put(c, x * 2, y, 0xFA, well);
				steel_put(c, x * 2 + 1, y, ' ', well);
			}
		}
	}
	if (!t->over) {
		for (r = 0; r < 4; r++)
			for (k = 0; k < 4; k++)
				if (bricks_cell(t->piece, t->rot, r, k) &&
				    t->py + r >= 0)
					block(c, (t->px + k) * 2, t->py + r,
					      piece_color[t->piece]);
	}

	steel_puts(c, 23, 1, "NEXT", panel);
	steel_rect(c, 23, 2, 10, 4, ' ', STEEL_ATTR(STEEL_BLACK, STEEL_BLACK));
	for (r = 0; r < 4; r++)
		for (k = 0; k < 4; k++)
			if (bricks_cell(t->next, 0, r, k))
				block(c, 24 + k * 2 - 1, 2 + r, piece_color[t->next]);
	snprintf(buf, sizeof(buf), "Score %u", t->score);
	steel_puts(c, 23, 8, buf, panel);
	snprintf(buf, sizeof(buf), "Best  %u", max(tw->best, t->score));
	steel_puts(c, 23, 9, buf, panel);
	snprintf(buf, sizeof(buf), "Lines %d", t->lines);
	steel_puts(c, 23, 11, buf, panel);
	snprintf(buf, sizeof(buf), "Level %d", t->level);
	steel_puts(c, 23, 12, buf, panel);
	if (tw->paused)
		steel_puts(c, 23, 15, " PAUSED ", STEEL_ATTR(STEEL_WHITE, STEEL_BLUE));
	if (t->over) {
		steel_puts(c, 4, 9, "  GAME OVER  ", STEEL_ATTR(STEEL_WHITE, STEEL_RED));
		steel_puts(c, 4, 10, " F2: new game ", panel);
	}
}

static bool bricks_key(struct win *w, int key, u8 mods)
{
	struct tw *tw = DESK_PRIV(w, struct tw);

	(void)mods;
	if (key == KEY_F(2)) {
		new_game(w);
		return true;
	}
	if (key == 'p' || key == 'P') {
		tw->paused = !tw->paused;
		return true;
	}
	if (tw->paused || tw->g.over)
		return false;
	switch (key) {
	case KEY_LEFT:
		bricks_move(&tw->g, -1);
		return true;
	case KEY_RIGHT:
		bricks_move(&tw->g, 1);
		return true;
	case KEY_UP:
		bricks_rotate(&tw->g);
		return true;
	case KEY_DOWN:
		bricks_step(&tw->g);
		tw->acc = 0;
		return true;
	case ' ':
		bricks_drop(&tw->g);
		tw->acc = 0;
		return true;
	default:
		return false;
	}
}

static void bricks_tick(struct win *w)
{
	struct tw *tw = DESK_PRIV(w, struct tw);

	if (tw->paused || tw->g.over)
		return;
	tw->acc += 40;
	if (tw->acc < bricks_interval(&tw->g))
		return;
	tw->acc = 0;
	bricks_step(&tw->g);
	if (tw->g.over && tw->g.score > tw->best)
		tw->best = tw->g.score;
}

static void bricks_command(struct win *w, int id)
{
	struct tw *tw = DESK_PRIV(w, struct tw);

	if (id == TT_NEW)
		new_game(w);
	else if (id == TT_PAUSE)
		tw->paused = !tw->paused;
}

const struct app app_bricks = {
	.name = "bricks",
	.title = "Bricks",
	.icon = { "[][]  ", "  [][]" },
	.icon_attr = { STEEL_ATTR(STEEL_WHITE, STEEL_RED),
		       STEEL_ATTR(STEEL_WHITE, STEEL_BLUE) },
	.cw = 36, .ch = BRICKS_H,
	.flags = APP_SINGLE,
	.tick_ms = 40,
	.menus = menus, .nmenus = ARRAY_SIZE(menus),
	.open = bricks_open,
	.paint = bricks_paint,
	.key = bricks_key,
	.tick = bricks_tick,
	.command = bricks_command,
};
