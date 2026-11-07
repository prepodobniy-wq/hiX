/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * desk chess: the rules and the computer player, no drawing and no input.
 *
 * Squares are 0..63 with a1 = 0, b1 = 1 ... h8 = 63 (rank * 8 + file).
 * A piece is its type, plus CH_BLACK for black; 0 is an empty square.
 * The engine is not re-entrant: the search keeps its move lists in static
 * memory (the desktop is a single thread, and this keeps the stack small).
 */
#ifndef _DESK_CHESS_H
#define _DESK_CHESS_H

#include <hix/types.h>
#include <hix/linkage.h>

HIX_BEGIN_C

enum { CH_NONE, CH_PAWN, CH_KNIGHT, CH_BISHOP, CH_ROOK, CH_QUEEN, CH_KING };
#define CH_BLACK	8
#define CH_TYPE(p)	((p) & 7)
#define CH_COLOR(p)	(((p) & CH_BLACK) ? 1 : 0)
#define CH_WHITE_SIDE	0
#define CH_BLACK_SIDE	1

#define CH_SQ(file, rank)	((rank) * 8 + (file))
#define CH_FILE(sq)		((sq) & 7)
#define CH_RANK(sq)		((sq) >> 3)

/* move flags */
#define CF_CAPTURE	0x01
#define CF_EP		0x02	/* en passant capture */
#define CF_CASTLE	0x04
#define CF_DOUBLE	0x08	/* pawn two-step */
#define CF_PROMO	0x10

struct chess_move {
	u8 from, to;
	u8 promo;		/* CH_QUEEN.. when CF_PROMO */
	u8 flags;
};

#define CH_MAX_MOVES	256
#define CH_MAX_HIST	1024

struct chess_hist {
	struct chess_move m;
	u8 captured;
	u8 castle;
	s8 ep;
	u8 halfmove;
	u32 hash;
};

struct chess {
	u8 sq[64];
	int turn;		/* CH_WHITE_SIDE or CH_BLACK_SIDE */
	u8 castle;		/* 1 white short, 2 white long, 4 black short, 8 black long */
	int ep;			/* square a pawn may capture onto, or -1 */
	int halfmove;		/* plies since a capture or pawn move */
	int fullmove;
	int nhist;
	struct chess_hist hist[CH_MAX_HIST];
	u32 hash;
};

enum chess_status {
	CS_ONGOING, CS_CHECKMATE, CS_STALEMATE, CS_DRAW_50, CS_DRAW_REPEAT,
	CS_DRAW_MATERIAL,
};

void chess_init(struct chess *g);
bool chess_load_fen(struct chess *g, const char *fen);
/* legal moves (the board is changed and restored while testing them) */
int chess_moves(struct chess *g, struct chess_move *out);
/* apply a move that is in the legal list; false if it is not */
bool chess_play(struct chess *g, const struct chess_move *m);
bool chess_undo(struct chess *g);
bool chess_in_check(const struct chess *g, int color);
enum chess_status chess_status(struct chess *g);
/* the legal move from @from to @to (promotion piece @promo, 0 = queen) */
bool chess_find_move(struct chess *g, int from, int to, int promo,
		     struct chess_move *out);
/* standard algebraic notation of a legal move, before it is played */
int chess_san(struct chess *g, const struct chess_move *m, char *out, size_t n);
const char *chess_status_text(enum chess_status s, int turn);

/* evaluation in centipawns for the side to move */
int chess_eval(const struct chess *g);
/*
 * The computer's move: iterative deepening alpha-beta to @depth plies (and a
 * quiet search on top), giving up after @max_nodes. @noise makes weaker
 * levels pick among near equal moves. False only if there is no legal move.
 */
bool chess_best(struct chess *g, int depth, unsigned long max_nodes, int noise,
		struct chess_move *out);
void chess_seed(u32 seed);
unsigned long chess_perft(struct chess *g, int depth);

HIX_END_C

#endif /* _DESK_CHESS_H */
