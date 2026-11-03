// SPDX-License-Identifier: GPL-2.0-only
/*
 * desk: Snake. Arrow keys (or WASD) steer, P pauses, F2 starts over.
 * The field is 30x18 cells drawn two characters wide.
 */
#include <hix/kernel.h>
#include <desk/desk.h>
#include <desk/games.h>

/* ---- the rules ------------------------------------------------------- */

#define CELLS	(SNAKE_W * SNAKE_H)

static int seg(const struct snake *s, int k)
{
	/* k = 0 is the head, k = len - 1 the tail */
	return s->body[(s->head - k + CELLS * 2) % CELLS];
}

bool snake_occupies(const struct snake *s, int x, int y)
{
	int k, pos = y * SNAKE_W + x;

	for (k = 0; k < s->len; k++)
		if (seg(s, k) == pos)
			return true;
	return false;
}

static void place_food(struct snake *s)
{
	int tries;

	if (s->len >= CELLS) {
		s->won = s->over = true;
		return;
	}
	for (tries = 0; tries < 1000; tries++) {
		int pos = (int)(desk_rand() % CELLS);

		if (!snake_occupies(s, pos % SNAKE_W, pos / SNAKE_W)) {
			s->food = pos;
			return;
		}
	}
	for (tries = 0; tries < CELLS; tries++) {	/* crowded: scan */
		if (!snake_occupies(s, tries % SNAKE_W, tries / SNAKE_W)) {
			s->food = tries;
			return;
		}
	}
}

void snake_init(struct snake *s)
{
	memset(s, 0, sizeof(*s));
	s->len = 3;
	s->dir = s->queued = DIR_RIGHT;
	s->head = 2;
	s->body[0] = (u16)((SNAKE_H / 2) * SNAKE_W + 4);
	s->body[1] = s->body[0] + 1;
	s->body[2] = s->body[0] + 2;
	place_food(s);
}

void snake_turn(struct snake *s, int dir)
{
	/* judged against the heading, so two quick turns cannot reverse us */
	if ((dir ^ s->dir) == 2 || dir == s->dir)
		return;
	s->queued = dir;
}

void snake_step(struct snake *s)
{
	int hx, hy, nx, ny, pos, k;
	bool grow;

	if (s->over)
		return;
	s->dir = s->queued;
	hx = seg(s, 0) % SNAKE_W;
	hy = seg(s, 0) / SNAKE_W;
	nx = hx + (s->dir == DIR_RIGHT) - (s->dir == DIR_LEFT);
	ny = hy + (s->dir == DIR_DOWN) - (s->dir == DIR_UP);
	if (nx < 0 || ny < 0 || nx >= SNAKE_W || ny >= SNAKE_H) {
		s->over = true;
		return;
	}
	pos = ny * SNAKE_W + nx;
	grow = pos == s->food;
	/* the tail moves away this step unless we are growing */
	for (k = 0; k < s->len - (grow ? 0 : 1); k++) {
		if (seg(s, k) == pos) {
			s->over = true;
			return;
		}
	}
	s->head = (s->head + 1) % CELLS;
	s->body[s->head] = (u16)pos;
	if (grow) {
		s->len++;
		s->score += 10;
		place_food(s);
	}
}

/* ---- the window ------------------------------------------------------ */

enum { SN_NEW = 1, SN_PAUSE, SN_SLOW, SN_NORMAL, SN_FAST };

struct sw {
	struct snake g;
	bool paused;
	int speed_ms, acc;
	u32 best;
};

static const struct menu_item game_items[] = {
	{ "New", SN_NEW, "F2" },
	{ "Pause", SN_PAUSE, "P" },
	{ "", 0, NULL },
	{ "Slow", SN_SLOW, NULL },
	{ "Normal", SN_NORMAL, NULL },
	{ "Fast", SN_FAST, NULL },
	{ "", 0, NULL },
	{ "Exit", CMD_CLOSE, "Alt+F4" },
};
static const struct menu menus[] = {
	{ "Game", game_items, ARRAY_SIZE(game_items) },
	DESK_HELP_MENU,
};

static void set_speed(struct win *w, int ms)
{
	struct sw *sw = DESK_PRIV(w, struct sw);

	sw->speed_ms = ms;
}

static void new_game(struct win *w)
{
	struct sw *sw = DESK_PRIV(w, struct sw);

	if (sw->g.score > sw->best)
		sw->best = sw->g.score;
	snake_init(&sw->g);
	sw->paused = false;
}

static void snake_open(struct win *w, const char *arg)
{
	struct sw *sw = DESK_PRIV(w, struct sw);

	(void)arg;
	sw->speed_ms = 120;
	new_game(w);
}

static void snake_paint(struct win *w, struct steel_surface *c)
{
	struct sw *sw = DESK_PRIV(w, struct sw);
	const struct snake *s = &sw->g;
	const u8 field = STEEL_ATTR(STEEL_DGRAY, STEEL_BLACK);
	char buf[64];
	int x, y, k;

	snprintf(buf, sizeof(buf), " Score %-5u  Best %-5u  Length %-3d%s",
		 s->score, max(sw->best, s->score), s->len,
		 sw->paused ? "  [PAUSED]" : "");
	steel_fill(c, ' ', STEEL_ATTR(STEEL_BLACK, STEEL_LGRAY));
	steel_puts(c, 0, 0, buf, STEEL_ATTR(STEEL_BLACK, STEEL_LGRAY));

	for (y = 0; y < SNAKE_H; y++)
		for (x = 0; x < SNAKE_W; x++) {
			steel_put(c, x * 2, 1 + y, 0xFA, field);
			steel_put(c, x * 2 + 1, 1 + y, ' ', field);
		}
	steel_put(c, (s->food % SNAKE_W) * 2, 1 + s->food / SNAKE_W, 0x03,
		  STEEL_ATTR(STEEL_LRED, STEEL_BLACK));
	for (k = s->len - 1; k >= 0; k--) {
		int pos = seg(s, k);
		u8 attr = k == 0 ? STEEL_ATTR(STEEL_YELLOW, STEEL_BLACK) :
				   STEEL_ATTR(STEEL_LGREEN, STEEL_BLACK);

		steel_put(c, (pos % SNAKE_W) * 2, 1 + pos / SNAKE_W, 0xDB, attr);
		steel_put(c, (pos % SNAKE_W) * 2 + 1, 1 + pos / SNAKE_W, 0xDB,
			  attr);
	}
	if (s->over) {
		const char *msg = s->won ? "  YOU WIN!  " : "  GAME OVER  ";

		steel_puts(c, (c->w - (int)strlen(msg)) / 2, c->h / 2, msg,
			   STEEL_ATTR(STEEL_WHITE, STEEL_RED));
		steel_puts(c, (c->w - 16) / 2, c->h / 2 + 1, " F2: play again ",
			   STEEL_ATTR(STEEL_BLACK, STEEL_LGRAY));
	}
}

static bool snake_key(struct win *w, int key, u8 mods)
{
	struct sw *sw = DESK_PRIV(w, struct sw);

	(void)mods;
	switch (key) {
	case KEY_UP: case 'w': case 'W':
		snake_turn(&sw->g, DIR_UP);
		return true;
	case KEY_DOWN: case 's': case 'S':
		snake_turn(&sw->g, DIR_DOWN);
		return true;
	case KEY_LEFT: case 'a': case 'A':
		snake_turn(&sw->g, DIR_LEFT);
		return true;
	case KEY_RIGHT: case 'd': case 'D':
		snake_turn(&sw->g, DIR_RIGHT);
		return true;
	case 'p': case 'P':
		sw->paused = !sw->paused;
		return true;
	case KEY_F(2):
		new_game(w);
		return true;
	default:
		return false;
	}
}

static void snake_tick(struct win *w)
{
	struct sw *sw = DESK_PRIV(w, struct sw);

	/* the window ticks every 40 ms; a step happens every speed_ms */
	if (sw->paused || sw->g.over)
		return;
	sw->acc += 40;
	if (sw->acc < sw->speed_ms)
		return;
	sw->acc = 0;
	snake_step(&sw->g);
	if (sw->g.over && sw->g.score > sw->best)
		sw->best = sw->g.score;
}

static void snake_command(struct win *w, int id)
{
	struct sw *sw = DESK_PRIV(w, struct sw);

	switch (id) {
	case SN_NEW:
		new_game(w);
		break;
	case SN_PAUSE:
		sw->paused = !sw->paused;
		break;
	case SN_SLOW:
		set_speed(w, 200);
		break;
	case SN_NORMAL:
		set_speed(w, 120);
		break;
	case SN_FAST:
		set_speed(w, 80);
		break;
	}
}

const struct app app_snake = {
	.name = "snake",
	.title = "Snake",
	.icon = { "\xDB\xDB\xDB\xDB\xDB\x03", "\xFA\xFA\xFA\xFA\xFA\xFA" },
	.icon_attr = { STEEL_ATTR(STEEL_GREEN, STEEL_WHITE),
		       STEEL_ATTR(STEEL_DGRAY, STEEL_WHITE) },
	.cw = SNAKE_W * 2, .ch = SNAKE_H + 1,
	.flags = APP_SINGLE,
	.tick_ms = 40,
	.menus = menus, .nmenus = ARRAY_SIZE(menus),
	.open = snake_open,
	.paint = snake_paint,
	.key = snake_key,
	.tick = snake_tick,
	.command = snake_command,
};
