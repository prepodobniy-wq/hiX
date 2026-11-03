// SPDX-License-Identifier: GPL-2.0-only
/* Tests for the chess rules and the computer player. */
#include "hosttest.h"
#include <string.h>
#include <hix/kernel.h>
#include <desk/chess.h>

static struct chess G;

static unsigned long perft_fen(const char *fen, int depth)
{
	if (!chess_load_fen(&G, fen))
		return 0;
	return chess_perft(&G, depth);
}

#define START "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
#define KIWI "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"
#define POS3 "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"
#define POS4 "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"
#define POS5 "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"

static void test_perft(void)
{
	printf("-- perft: the standard positions\n");
	CHECK(perft_fen(START, 1) == 20);
	CHECK(perft_fen(START, 2) == 400);
	CHECK(perft_fen(START, 3) == 8902);
	CHECK(perft_fen(START, 4) == 197281);
	CHECK(perft_fen(KIWI, 1) == 48);
	CHECK(perft_fen(KIWI, 2) == 2039);
	CHECK(perft_fen(KIWI, 3) == 97862);
	CHECK(perft_fen(POS3, 1) == 14);
	CHECK(perft_fen(POS3, 3) == 2812);
	CHECK(perft_fen(POS3, 4) == 43238);
	CHECK(perft_fen(POS4, 1) == 6);
	CHECK(perft_fen(POS4, 2) == 264);
	CHECK(perft_fen(POS4, 3) == 9467);
	CHECK(perft_fen(POS5, 1) == 44);
	CHECK(perft_fen(POS5, 2) == 1486);
	CHECK(perft_fen(POS5, 3) == 62379);
	/* perft must leave the position untouched */
	chess_init(&G);
	chess_perft(&G, 3);
	CHECK(G.nhist == 0 && G.turn == 0 && G.castle == 15 && G.ep == -1);
	CHECK(G.sq[CH_SQ(4, 0)] == CH_KING && G.sq[CH_SQ(4, 7)] == (CH_KING | CH_BLACK));
}

static bool play(const char *uci)
{
	struct chess_move m;
	int promo = 0;

	if (strlen(uci) == 5)
		promo = uci[4] == 'q' ? CH_QUEEN : uci[4] == 'r' ? CH_ROOK :
			uci[4] == 'b' ? CH_BISHOP : CH_KNIGHT;
	if (!chess_find_move(&G, CH_SQ(uci[0] - 'a', uci[1] - '1'),
			     CH_SQ(uci[2] - 'a', uci[3] - '1'), promo, &m))
		return false;
	return chess_play(&G, &m);
}

static bool same_position(const struct chess *a, const struct chess *b)
{
	return !memcmp(a->sq, b->sq, 64) && a->turn == b->turn &&
	       a->castle == b->castle && a->ep == b->ep &&
	       a->halfmove == b->halfmove && a->fullmove == b->fullmove &&
	       a->hash == b->hash;
}

static void test_rules(void)
{
	static struct chess before;
	char san[16];
	struct chess_move m;

	printf("-- rules\n");
	chess_init(&G);
	CHECK(!play("e2e5"));			/* illegal */
	CHECK(!play("e7e5"));			/* wrong side */
	CHECK(play("e2e4") && G.turn == 1 && G.ep == CH_SQ(4, 2));
	CHECK(play("a7a6") && G.ep == -1);
	CHECK(G.fullmove == 2);

	/* undo restores everything, move by move */
	chess_init(&G);
	memcpy(&before, &G, sizeof(G));
	CHECK(play("e2e4") && play("d7d5") && play("e4d5") && play("g8f6"));
	CHECK(chess_undo(&G) && chess_undo(&G) && chess_undo(&G) && chess_undo(&G));
	CHECK(same_position(&G, &before));
	CHECK(!chess_undo(&G));

	/* castling: both sides, rights lost when the king or rook moves */
	chess_load_fen(&G, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
	CHECK(play("e1g1") && G.sq[CH_SQ(6, 0)] == CH_KING && G.sq[CH_SQ(5, 0)] == CH_ROOK);
	CHECK(G.castle == 12);
	CHECK(play("e8c8") && G.sq[CH_SQ(2, 7)] == (CH_KING | CH_BLACK));
	CHECK(G.sq[CH_SQ(3, 7)] == (CH_ROOK | CH_BLACK) && G.castle == 0);
	chess_undo(&G);
	chess_undo(&G);
	CHECK(G.castle == 15 && G.sq[CH_SQ(7, 0)] == CH_ROOK &&
	      G.sq[CH_SQ(0, 7)] == (CH_ROOK | CH_BLACK));
	chess_load_fen(&G, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
	CHECK(play("h1h2") && G.castle == 14);
	/* no castling through or out of check, or over a piece */
	chess_load_fen(&G, "4k3/8/8/8/8/5r2/8/R3K2R w KQ - 0 1");
	CHECK(!play("e1g1"));			/* f1 is attacked */
	CHECK(play("e1c1"));			/* the long side is free */
	chess_load_fen(&G, "4k3/8/8/8/8/8/8/R3K1NR w KQ - 0 1");
	CHECK(!play("e1g1"));
	chess_load_fen(&G, "4k3/4r3/8/8/8/8/8/R3K2R w KQ - 0 1");
	CHECK(!play("e1g1") && !play("e1c1"));	/* in check */

	/* en passant, and only right after the double step */
	chess_load_fen(&G, "4k3/8/8/8/3p4/8/4P3/4K3 w - - 0 1");
	CHECK(play("e2e4") && play("d4e3") && G.sq[CH_SQ(4, 3)] == 0);
	CHECK(G.sq[CH_SQ(4, 2)] == (CH_PAWN | CH_BLACK));
	chess_undo(&G);
	CHECK(G.sq[CH_SQ(4, 3)] == CH_PAWN && G.sq[CH_SQ(4, 2)] == 0);
	chess_load_fen(&G, "4k3/8/8/8/3p4/8/4P3/4K3 w - - 0 1");
	play("e1d1");
	play("e8d8");
	play("d1e1");
	play("d8e8");
	CHECK(!chess_find_move(&G, CH_SQ(3, 3), CH_SQ(4, 2), 0, &m));

	/* a pinned piece cannot move off the line */
	chess_load_fen(&G, "4k3/4r3/8/8/8/8/4N3/4K3 w - - 0 1");
	CHECK(!play("e2c3") && !play("e2g3"));

	/* promotion, and the choice of piece */
	chess_load_fen(&G, "8/P6k/8/8/8/8/8/K7 w - - 0 1");
	CHECK(chess_find_move(&G, CH_SQ(0, 6), CH_SQ(0, 7), CH_KNIGHT, &m));
	CHECK(m.flags & CF_PROMO && m.promo == CH_KNIGHT);
	CHECK(chess_play(&G, &m) && G.sq[CH_SQ(0, 7)] == CH_KNIGHT);
	chess_undo(&G);
	CHECK(G.sq[CH_SQ(0, 6)] == CH_PAWN && G.sq[CH_SQ(0, 7)] == 0);
	CHECK(play("a7a8") && G.sq[CH_SQ(0, 7)] == CH_QUEEN);	/* default: queen */

	/* check detection */
	chess_load_fen(&G, "4k3/8/8/8/8/8/4R3/4K3 b - - 0 1");
	CHECK(chess_in_check(&G, 1) && !chess_in_check(&G, 0));
	(void)san;
}

static void test_status(void)
{
	printf("-- game status\n");
	chess_load_fen(&G, "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
	CHECK(chess_status(&G) == CS_CHECKMATE);
	CHECK(!strcmp(chess_status_text(CS_CHECKMATE, G.turn), "Checkmate - Black wins"));
	chess_load_fen(&G, "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
	CHECK(chess_status(&G) == CS_STALEMATE);
	chess_load_fen(&G, "8/8/8/4k3/8/8/8/4K3 w - - 0 1");
	CHECK(chess_status(&G) == CS_DRAW_MATERIAL);
	chess_load_fen(&G, "8/8/8/4k3/8/8/4N3/4K3 w - - 0 1");
	CHECK(chess_status(&G) == CS_DRAW_MATERIAL);
	chess_load_fen(&G, "8/8/8/4k3/8/8/4NN2/4K3 w - - 0 1");
	CHECK(chess_status(&G) == CS_ONGOING);
	chess_load_fen(&G, "8/8/8/4k3/8/8/4R3/4K3 w - - 100 80");
	CHECK(chess_status(&G) == CS_DRAW_50);
	chess_load_fen(&G, "8/8/8/4k3/8/8/4R3/4K3 w - - 99 80");
	CHECK(chess_status(&G) == CS_ONGOING);

	/* threefold repetition: the knights go out and come back twice */
	chess_init(&G);
	CHECK(play("g1f3") && play("g8f6") && play("f3g1") && play("f6g8"));
	CHECK(chess_status(&G) == CS_ONGOING);	/* second time */
	CHECK(play("g1f3") && play("g8f6") && play("f3g1") && play("f6g8"));
	CHECK(chess_status(&G) == CS_DRAW_REPEAT);
	chess_undo(&G);
	CHECK(chess_status(&G) == CS_ONGOING);
}

static void test_san(void)
{
	char s[16];
	struct chess_move m;

	printf("-- notation\n");
	chess_init(&G);
#define SAN(from, to, want) do {					\
	CHECK(chess_find_move(&G, CH_SQ(from[0] - 'a', from[1] - '1'),	\
			      CH_SQ(to[0] - 'a', to[1] - '1'), 0, &m));	\
	chess_san(&G, &m, s, sizeof(s));				\
	CHECK_STR(s, want);						\
	chess_play(&G, &m);						\
} while (0)
	SAN("e2", "e4", "e4");
	SAN("e7", "e5", "e5");
	SAN("g1", "f3", "Nf3");
	SAN("b8", "c6", "Nc6");
	SAN("f1", "b5", "Bb5");
	SAN("a7", "a6", "a6");
	SAN("b5", "c6", "Bxc6");
	SAN("d7", "c6", "dxc6");
	SAN("e1", "g1", "O-O");
	SAN("d8", "d6", "Qd6");

	/* two knights can reach the square: say which */
	chess_load_fen(&G, "4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1");
	SAN("b1", "d2", "Nbd2");
	chess_load_fen(&G, "4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1");
	SAN("f1", "d2", "Nfd2");
	/* same file: use the rank */
	chess_load_fen(&G, "4k3/8/8/1N6/8/1N6/8/4K3 w - - 0 1");
	SAN("b3", "d4", "N3d4");
	/* check, mate, promotion, castling long, en passant */
	chess_load_fen(&G, "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1");
	SAN("a1", "a8", "Ra8#");
	chess_load_fen(&G, "4k3/8/8/8/8/8/4R3/4K3 w - - 0 1");
	SAN("e2", "e7", "Re7+");
	chess_load_fen(&G, "7k/P7/8/8/8/8/8/K7 w - - 0 1");
	CHECK(chess_find_move(&G, CH_SQ(0, 6), CH_SQ(0, 7), CH_QUEEN, &m));
	chess_san(&G, &m, s, sizeof(s));
	CHECK_STR(s, "a8=Q+");
	chess_load_fen(&G, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
	SAN("e1", "c1", "O-O-O");
	chess_load_fen(&G, "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
	SAN("e5", "d6", "exd6");
	/* san must not change the position */
	{
		static struct chess before;

		chess_init(&G);
		memcpy(&before, &G, sizeof(G));
		chess_find_move(&G, 12, 28, 0, &m);
		chess_san(&G, &m, s, sizeof(s));
		CHECK(same_position(&G, &before));
	}
}

static void test_engine(void)
{
	struct chess_move m, list[CH_MAX_MOVES];
	int i, plies;

	printf("-- the computer player\n");
	chess_seed(1);

	/* mate in one */
	chess_load_fen(&G, "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1");
	CHECK(chess_best(&G, 3, 0, 0, &m));
	CHECK(m.from == CH_SQ(0, 0) && m.to == CH_SQ(0, 7));
	/* mate in two (Qh5+ style ladder): two rooks */
	chess_load_fen(&G, "7k/8/8/8/8/8/R7/1R4K1 w - - 0 1");
	CHECK(chess_best(&G, 4, 0, 0, &m));
	CHECK(chess_play(&G, &m));
	CHECK(chess_best(&G, 4, 0, 0, &m));	/* black replies, then white mates */
	CHECK(chess_play(&G, &m));
	CHECK(chess_best(&G, 4, 0, 0, &m));
	CHECK(chess_play(&G, &m));
	CHECK(chess_status(&G) == CS_CHECKMATE);
	/* takes a free queen */
	chess_load_fen(&G, "4k3/8/8/3q4/8/8/8/3RK3 w - - 0 1");
	CHECK(chess_best(&G, 2, 0, 0, &m) && m.to == CH_SQ(3, 4));
	/* a queen attacked by a pawn is saved: black cannot take it afterwards */
	chess_load_fen(&G, "4k3/8/8/2p5/3Q4/8/8/4K3 w - - 0 1");
	CHECK(chess_best(&G, 2, 0, 0, &m));
	CHECK(chess_play(&G, &m));
	{
		int n = chess_moves(&G, list), k, queen_lost = 0;

		for (k = 0; k < n; k++)
			if ((list[k].flags & CF_CAPTURE) &&
			    CH_TYPE(G.sq[list[k].to]) == CH_QUEEN)
				queen_lost = 1;
		CHECK(!queen_lost);
	}
	/* escapes a check by the only move, and plays when only one move exists */
	chess_load_fen(&G, "7k/8/8/8/8/8/6q1/7K w - - 0 1");
	CHECK(chess_best(&G, 3, 0, 0, &m) && m.from == CH_SQ(7, 0) && m.to == CH_SQ(6, 1));
	/* no legal move: false */
	chess_load_fen(&G, "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
	CHECK(!chess_best(&G, 3, 0, 0, &m));

	/* the node budget is honoured (and a move is still returned) */
	chess_init(&G);
	CHECK(chess_best(&G, 6, 2000, 0, &m));
	CHECK(chess_find_move(&G, m.from, m.to, m.promo, &m));

	/* evaluation: symmetrical start, material counts */
	chess_init(&G);
	CHECK(chess_eval(&G) == 0);
	chess_load_fen(&G, "4k3/8/8/8/8/8/8/Q3K3 w - - 0 1");
	CHECK(chess_eval(&G) > 800);
	chess_load_fen(&G, "4k3/8/8/8/8/8/8/Q3K3 b - - 0 1");
	CHECK(chess_eval(&G) < -800);

	/* weak and strong play whole games without ever making an illegal move */
	for (i = 0; i < 4; i++) {
		chess_init(&G);
		for (plies = 0; plies < 160; plies++) {
			int n;

			if (chess_status(&G) != CS_ONGOING)
				break;
			n = chess_moves(&G, list);
			CHECK(n > 0);
			CHECK(chess_best(&G, 1 + (i + plies) % 3, 5000, i == 0 ? 60 : 0, &m));
			CHECK(chess_find_move(&G, m.from, m.to, m.promo, &m));
			CHECK(chess_play(&G, &m));
		}
		CHECK(plies > 10);
	}
}

int main(void)
{
	printf("test_chess\n");
	test_perft();
	test_rules();
	test_status();
	test_san();
	test_engine();
	TEST_DONE();
}
