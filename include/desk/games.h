/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * desk: the rules of the games, kept apart from their windows.
 *
 * Each game is a small state machine over plain data: no drawing, no
 * input, no clock. The windows in apps/ turn keys and ticks into calls
 * here, and tests/test_desk.c plays the rules directly.
 */
#ifndef _DESK_GAMES_H
#define _DESK_GAMES_H

#include <hix/types.h>

/* ---- Minefield ----------------------------------------------------- */

#define MINES_MAX_W	30
#define MINES_MAX_H	16

enum { MS_READY, MS_PLAYING, MS_WON, MS_LOST };
enum { MC_HIDDEN, MC_OPEN, MC_FLAG };

struct mines {
	int w, h, nmines;
	int status;
	int flags, opened;
	int hit_x, hit_y;			/* the mine that was stepped on */
	u8 mine[MINES_MAX_W * MINES_MAX_H];
	u8 state[MINES_MAX_W * MINES_MAX_H];	/* MC_* */
	u8 adj[MINES_MAX_W * MINES_MAX_H];
};

void mines_init(struct mines *m, int w, int h, int nmines);
/* the first reveal places the mines, never under the click */
void mines_reveal(struct mines *m, int x, int y);
void mines_flag(struct mines *m, int x, int y);

/* ---- Bricks ---------------------------------------------------------- */

#define BRICKS_W	10
#define BRICKS_H	20

struct bricks {
	u8 board[BRICKS_H][BRICKS_W];	/* 0 = empty, else piece number + 1 */
	int piece, rot, px, py;		/* the falling piece; py may be < 0 */
	int next;
	u32 score;
	int lines, level;
	bool over;
};

void bricks_init(struct bricks *t);
bool bricks_move(struct bricks *t, int dx);
bool bricks_rotate(struct bricks *t);
/* gravity one row; locks the piece and spawns the next when it lands */
bool bricks_step(struct bricks *t);
void bricks_drop(struct bricks *t);	/* hard drop */
bool bricks_cell(int piece, int rot, int r, int c);
/* drop interval in milliseconds for the current level */
int bricks_interval(const struct bricks *t);

/* ---- Snake ----------------------------------------------------------- */

#define SNAKE_W		30
#define SNAKE_H		18

enum { DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT };

struct snake {
	u16 body[SNAKE_W * SNAKE_H];	/* ring buffer of y * SNAKE_W + x */
	int head, len;			/* head = index of the newest segment */
	int dir, queued;		/* queued: next turn, applied on a step */
	int food;
	u32 score;
	bool over, won;
};

void snake_init(struct snake *s);
void snake_turn(struct snake *s, int dir);
void snake_step(struct snake *s);
bool snake_occupies(const struct snake *s, int x, int y);

/* ---- Reversi --------------------------------------------------------- */

enum { RV_EMPTY, RV_BLACK, RV_WHITE };

struct reversi {
	u8 b[8][8];
	int turn;
	bool over;
};

void reversi_init(struct reversi *r);
int reversi_flips(const struct reversi *r, int x, int y, int who);
bool reversi_play(struct reversi *r, int x, int y);	/* for r->turn */
bool reversi_has_move(const struct reversi *r, int who);
int reversi_count(const struct reversi *r, int who);
/* the computer's choice for r->turn, false if it has to pass */
bool reversi_choose(const struct reversi *r, int *x, int *y);
/* give the turn to the other side, or end the game if nobody can move;
 * returns true when the turn passed without a move (a "pass") */
bool reversi_advance(struct reversi *r);

#endif /* _DESK_GAMES_H */
