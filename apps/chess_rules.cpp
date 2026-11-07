// SPDX-License-Identifier: GPL-2.0-only
/*
 * desk chess: rules, notation and the computer player. See desk/chess.h.
 *
 * Move generation is pseudo-legal first, then each move is made and the king
 * checked: simple, and fast enough for a search of a few plies.
 */
/*
 * This is the first hix code in C++ (freestanding: no exceptions, no RTTI, no
 * standard library). The interface in desk/chess.h is plain C, so the window
 * code in chess.c does not care which language the rules are written in.
 */
extern "C" {
#include <hix/kernel.h>
}
#include <desk/chess.h>

#define MAX_PLY		24
#define MATE		30000
#define INF		32000

static constexpr s8 knight_d[8][2] = {
	{ 1, 2 }, { 2, 1 }, { 2, -1 }, { 1, -2 },
	{ -1, -2 }, { -2, -1 }, { -2, 1 }, { -1, 2 },
};
static constexpr s8 king_d[8][2] = {
	{ 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 },
	{ -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 },
};
/* sliders: 0-3 rook directions, 4-7 bishop directions of king_d */

static inline bool on_board(int f, int r)
{
	return f >= 0 && f < 8 && r >= 0 && r < 8;
}

/* ---- setup ----------------------------------------------------------- */

static u32 compute_hash(const struct chess *g)
{
	u32 h = 2166136261u;
	int i;

	for (i = 0; i < 64; i++)
		h = (h ^ g->sq[i]) * 16777619u;
	h = (h ^ (u32)g->turn) * 16777619u;
	h = (h ^ g->castle) * 16777619u;
	h = (h ^ (u32)(g->ep + 1)) * 16777619u;
	return h;
}

void chess_init(struct chess *g)
{
	static const u8 back[8] = { CH_ROOK, CH_KNIGHT, CH_BISHOP, CH_QUEEN,
				    CH_KING, CH_BISHOP, CH_KNIGHT, CH_ROOK };
	int f;

	memset(g, 0, sizeof(*g));
	for (f = 0; f < 8; f++) {
		g->sq[CH_SQ(f, 0)] = back[f];
		g->sq[CH_SQ(f, 1)] = CH_PAWN;
		g->sq[CH_SQ(f, 6)] = CH_PAWN | CH_BLACK;
		g->sq[CH_SQ(f, 7)] = back[f] | CH_BLACK;
	}
	g->castle = 15;
	g->ep = -1;
	g->fullmove = 1;
	g->hash = compute_hash(g);
}

bool chess_load_fen(struct chess *g, const char *fen)
{
	int f = 0, r = 7;
	const char *p = fen;

	memset(g, 0, sizeof(*g));
	g->ep = -1;
	for (; *p && *p != ' '; p++) {
		char c = *p;

		if (c == '/') {
			if (f != 8)
				return false;
			f = 0;
			r--;
		} else if (c >= '1' && c <= '8') {
			f += c - '0';
		} else {
			static const char letters[] = "pnbrqk";
			const char *at = strchr(letters, c | 0x20);
			int piece;

			if (!at || !*at || f > 7 || r < 0)
				return false;
			piece = (int)(at - letters) + 1;
			if (!(c >= 'A' && c <= 'Z'))
				piece |= CH_BLACK;
			g->sq[CH_SQ(f, r)] = (u8)piece;
			f++;
		}
	}
	if (r != 0 || f != 8)
		return false;
	if (*p == ' ')
		p++;
	g->turn = *p == 'b' ? CH_BLACK_SIDE : CH_WHITE_SIDE;
	while (*p && *p != ' ')
		p++;
	if (*p == ' ')
		p++;
	for (; *p && *p != ' '; p++) {
		if (*p == 'K')
			g->castle |= 1;
		else if (*p == 'Q')
			g->castle |= 2;
		else if (*p == 'k')
			g->castle |= 4;
		else if (*p == 'q')
			g->castle |= 8;
	}
	if (*p == ' ')
		p++;
	if (*p >= 'a' && *p <= 'h' && p[1] >= '1' && p[1] <= '8')
		g->ep = CH_SQ(*p - 'a', p[1] - '1');
	while (*p && *p != ' ')
		p++;
	if (*p == ' ')
		p++;
	g->halfmove = (int)simple_strtoul(p, (char **)&p, 10);
	if (*p == ' ')
		p++;
	g->fullmove = (int)simple_strtoul(p, NULL, 10);
	if (g->fullmove < 1)
		g->fullmove = 1;
	g->hash = compute_hash(g);
	return true;
}

/* ---- attacks --------------------------------------------------------- */

static int king_square(const struct chess *g, int color)
{
	int i, king = CH_KING | (color ? CH_BLACK : 0);

	for (i = 0; i < 64; i++)
		if (g->sq[i] == king)
			return i;
	return -1;
}

static bool attacked(const struct chess *g, int sq, int by)
{
	int f = CH_FILE(sq), r = CH_RANK(sq), i, k;
	int side = by ? CH_BLACK : 0;
	int pr = by ? r + 1 : r - 1;		/* where an attacking pawn stands */

	for (i = -1; i <= 1; i += 2)
		if (on_board(f + i, pr) &&
		    g->sq[CH_SQ(f + i, pr)] == (CH_PAWN | side))
			return true;
	for (i = 0; i < 8; i++) {
		int nf = f + knight_d[i][0], nr = r + knight_d[i][1];

		if (on_board(nf, nr) && g->sq[CH_SQ(nf, nr)] == (CH_KNIGHT | side))
			return true;
		nf = f + king_d[i][0];
		nr = r + king_d[i][1];
		if (on_board(nf, nr) && g->sq[CH_SQ(nf, nr)] == (CH_KING | side))
			return true;
	}
	for (i = 0; i < 8; i++) {
		bool diagonal = king_d[i][0] && king_d[i][1];
		int nf = f + king_d[i][0], nr = r + king_d[i][1];

		for (k = 0; on_board(nf, nr); k++) {
			int p = g->sq[CH_SQ(nf, nr)];

			if (p) {
				if (CH_COLOR(p) == by) {
					int t = CH_TYPE(p);

					if (t == CH_QUEEN ||
					    t == (diagonal ? CH_BISHOP : CH_ROOK))
						return true;
				}
				break;
			}
			nf += king_d[i][0];
			nr += king_d[i][1];
		}
	}
	return false;
}

bool chess_in_check(const struct chess *g, int color)
{
	int k = king_square(g, color);

	return k >= 0 && attacked(g, k, !color);
}

/* ---- move generation ------------------------------------------------- */

static void add(struct chess_move *out, int *n, int from, int to, int flags,
		int promo)
{
	out[*n].from = (u8)from;
	out[*n].to = (u8)to;
	out[*n].flags = (u8)flags;
	out[*n].promo = (u8)promo;
	(*n)++;
}

static void add_pawn(struct chess_move *out, int *n, int from, int to, int flags)
{
	static const u8 promos[4] = { CH_QUEEN, CH_ROOK, CH_BISHOP, CH_KNIGHT };
	int r = CH_RANK(to), i;

	if (r == 0 || r == 7) {
		for (i = 0; i < 4; i++)
			add(out, n, from, to, flags | CF_PROMO, promos[i]);
	} else {
		add(out, n, from, to, flags, 0);
	}
}

/* every move that obeys the piece rules, king safety not yet checked */
static int gen_pseudo(const struct chess *g, struct chess_move *out,
		      bool captures_only)
{
	int n = 0, from, side = g->turn, enemy = !side;

	for (from = 0; from < 64; from++) {
		int p = g->sq[from], t, f, r, i;

		if (!p || CH_COLOR(p) != side)
			continue;
		t = CH_TYPE(p);
		f = CH_FILE(from);
		r = CH_RANK(from);
		if (t == CH_PAWN) {
			int dir = side ? -1 : 1, start = side ? 6 : 1;
			int nr = r + dir;

			if (on_board(f, nr) && !g->sq[CH_SQ(f, nr)]) {
				if (!captures_only || nr == 0 || nr == 7) {
					add_pawn(out, &n, from, CH_SQ(f, nr), 0);
					if (!captures_only && r == start &&
					    !g->sq[CH_SQ(f, nr + dir)])
						add(out, &n, from, CH_SQ(f, nr + dir),
						    CF_DOUBLE, 0);
				}
			}
			for (i = -1; i <= 1; i += 2) {
				int to;

				if (!on_board(f + i, nr))
					continue;
				to = CH_SQ(f + i, nr);
				if (g->sq[to] && CH_COLOR(g->sq[to]) == enemy)
					add_pawn(out, &n, from, to, CF_CAPTURE);
				else if (to == g->ep)
					add(out, &n, from, to, CF_CAPTURE | CF_EP, 0);
			}
			continue;
		}
		if (t == CH_KNIGHT || t == CH_KING) {
			for (i = 0; i < 8; i++) {
				const s8 *d = t == CH_KNIGHT ? knight_d[i] : king_d[i];
				int nf = f + d[0], nr = r + d[1], q, to;

				if (!on_board(nf, nr))
					continue;
				to = CH_SQ(nf, nr);
				q = g->sq[to];
				if (!q && !captures_only)
					add(out, &n, from, to, 0, 0);
				else if (q && CH_COLOR(q) == enemy)
					add(out, &n, from, to, CF_CAPTURE, 0);
			}
			if (t == CH_KING && !captures_only) {
				int home = side ? 7 : 0;
				int ks = side ? 4 : 1, qs = side ? 8 : 2;

				if (from != CH_SQ(4, home) || attacked(g, from, enemy))
					continue;
				if ((g->castle & ks) &&
				    g->sq[CH_SQ(7, home)] == (CH_ROOK | (side ? CH_BLACK : 0)) &&
				    !g->sq[CH_SQ(5, home)] && !g->sq[CH_SQ(6, home)] &&
				    !attacked(g, CH_SQ(5, home), enemy) &&
				    !attacked(g, CH_SQ(6, home), enemy))
					add(out, &n, from, CH_SQ(6, home), CF_CASTLE, 0);
				if ((g->castle & qs) &&
				    g->sq[CH_SQ(0, home)] == (CH_ROOK | (side ? CH_BLACK : 0)) &&
				    !g->sq[CH_SQ(1, home)] && !g->sq[CH_SQ(2, home)] &&
				    !g->sq[CH_SQ(3, home)] &&
				    !attacked(g, CH_SQ(3, home), enemy) &&
				    !attacked(g, CH_SQ(2, home), enemy))
					add(out, &n, from, CH_SQ(2, home), CF_CASTLE, 0);
			}
			continue;
		}
		/* sliders */
		for (i = 0; i < 8; i++) {
			bool diagonal = king_d[i][0] && king_d[i][1];
			int nf = f + king_d[i][0], nr = r + king_d[i][1];

			if (t == CH_BISHOP && !diagonal)
				continue;
			if (t == CH_ROOK && diagonal)
				continue;
			while (on_board(nf, nr)) {
				int to = CH_SQ(nf, nr), q = g->sq[to];

				if (!q) {
					if (!captures_only)
						add(out, &n, from, to, 0, 0);
				} else {
					if (CH_COLOR(q) == enemy)
						add(out, &n, from, to, CF_CAPTURE, 0);
					break;
				}
				nf += king_d[i][0];
				nr += king_d[i][1];
			}
		}
	}
	return n;
}

/* ---- make and unmake ------------------------------------------------- */

static void make(struct chess *g, const struct chess_move *m)
{
	struct chess_hist *u = &g->hist[g->nhist++];
	int piece = g->sq[m->from], captured = g->sq[m->to];
	int color = g->turn, home = color ? 7 : 0;

	u->m = *m;
	u->castle = g->castle;
	u->ep = (s8)g->ep;
	u->halfmove = (u8)g->halfmove;
	u->hash = g->hash;

	if (m->flags & CF_EP) {
		int cap = CH_SQ(CH_FILE(m->to), CH_RANK(m->from));

		captured = g->sq[cap];
		g->sq[cap] = 0;
	}
	u->captured = (u8)captured;
	g->sq[m->to] = (m->flags & CF_PROMO) ?
		(u8)(m->promo | (color ? CH_BLACK : 0)) : (u8)piece;
	g->sq[m->from] = 0;
	if (m->flags & CF_CASTLE) {
		if (m->to > m->from) {
			g->sq[CH_SQ(5, home)] = g->sq[CH_SQ(7, home)];
			g->sq[CH_SQ(7, home)] = 0;
		} else {
			g->sq[CH_SQ(3, home)] = g->sq[CH_SQ(0, home)];
			g->sq[CH_SQ(0, home)] = 0;
		}
	}
	if (CH_TYPE(piece) == CH_KING)
		g->castle &= (u8)~(color ? 12 : 3);
	/* a rook leaving or being taken on its corner ends that right */
	if (m->from == 0 || m->to == 0)
		g->castle &= (u8)~2;
	if (m->from == 7 || m->to == 7)
		g->castle &= (u8)~1;
	if (m->from == 56 || m->to == 56)
		g->castle &= (u8)~8;
	if (m->from == 63 || m->to == 63)
		g->castle &= (u8)~4;
	g->ep = (m->flags & CF_DOUBLE) ? (m->from + m->to) / 2 : -1;
	g->halfmove = (CH_TYPE(piece) == CH_PAWN || captured) ? 0 : g->halfmove + 1;
	if (color)
		g->fullmove++;
	g->turn = !color;
	g->hash = compute_hash(g);
}

static void unmake(struct chess *g)
{
	const struct chess_hist *u = &g->hist[--g->nhist];
	const struct chess_move *m = &u->m;
	int color, piece, home;

	g->turn = !g->turn;
	color = g->turn;
	home = color ? 7 : 0;
	if (color)
		g->fullmove--;
	piece = g->sq[m->to];
	if (m->flags & CF_PROMO)
		piece = CH_PAWN | (color ? CH_BLACK : 0);
	g->sq[m->from] = (u8)piece;
	if (m->flags & CF_EP) {
		g->sq[m->to] = 0;
		g->sq[CH_SQ(CH_FILE(m->to), CH_RANK(m->from))] = u->captured;
	} else {
		g->sq[m->to] = u->captured;
	}
	if (m->flags & CF_CASTLE) {
		if (m->to > m->from) {
			g->sq[CH_SQ(7, home)] = g->sq[CH_SQ(5, home)];
			g->sq[CH_SQ(5, home)] = 0;
		} else {
			g->sq[CH_SQ(0, home)] = g->sq[CH_SQ(3, home)];
			g->sq[CH_SQ(3, home)] = 0;
		}
	}
	g->castle = u->castle;
	g->ep = u->ep;
	g->halfmove = u->halfmove;
	g->hash = u->hash;
}

static int gen_legal(struct chess *g, struct chess_move *out, bool captures_only)
{
	int n = gen_pseudo(g, out, captures_only), i, k = 0;
	int side = g->turn;

	/* filter in place: k never passes i */
	for (i = 0; i < n; i++) {
		struct chess_move m = out[i];

		make(g, &m);
		if (!chess_in_check(g, side))
			out[k++] = m;
		unmake(g);
	}
	return k;
}

int chess_moves(struct chess *g, struct chess_move *out)
{
	return gen_legal(g, out, false);
}

bool chess_find_move(struct chess *g, int from, int to, int promo,
		     struct chess_move *out)
{
	struct chess_move list[CH_MAX_MOVES];
	int n = gen_legal(g, list, false), i;

	for (i = 0; i < n; i++) {
		if (list[i].from != from || list[i].to != to)
			continue;
		if ((list[i].flags & CF_PROMO) &&
		    list[i].promo != (promo ? promo : CH_QUEEN))
			continue;
		*out = list[i];
		return true;
	}
	return false;
}

bool chess_play(struct chess *g, const struct chess_move *m)
{
	struct chess_move found;

	if (g->nhist >= CH_MAX_HIST - MAX_PLY - 2)
		return false;
	if (!chess_find_move(g, m->from, m->to, m->promo, &found))
		return false;
	make(g, &found);
	return true;
}

bool chess_undo(struct chess *g)
{
	if (!g->nhist)
		return false;
	unmake(g);
	return true;
}

/* ---- the state of the game ------------------------------------------- */

static bool insufficient_material(const struct chess *g)
{
	int i, minors = 0;

	for (i = 0; i < 64; i++) {
		int t = CH_TYPE(g->sq[i]);

		if (t == CH_PAWN || t == CH_ROOK || t == CH_QUEEN)
			return false;
		if (t == CH_KNIGHT || t == CH_BISHOP)
			minors++;
	}
	return minors <= 1;
}

static int repetitions(const struct chess *g)
{
	int i, n = 1, lowest = g->nhist - g->halfmove;

	if (lowest < 0)
		lowest = 0;
	for (i = g->nhist - 1; i >= lowest; i--)
		if (g->hist[i].hash == g->hash)
			n++;
	return n;
}

enum chess_status chess_status(struct chess *g)
{
	struct chess_move list[CH_MAX_MOVES];

	if (!gen_legal(g, list, false))
		return chess_in_check(g, g->turn) ? CS_CHECKMATE : CS_STALEMATE;
	if (insufficient_material(g))
		return CS_DRAW_MATERIAL;
	if (g->halfmove >= 100)
		return CS_DRAW_50;
	if (repetitions(g) >= 3)
		return CS_DRAW_REPEAT;
	return CS_ONGOING;
}

const char *chess_status_text(enum chess_status s, int turn)
{
	switch (s) {
	case CS_CHECKMATE:
		return turn ? "Checkmate - White wins" : "Checkmate - Black wins";
	case CS_STALEMATE:
		return "Draw by stalemate";
	case CS_DRAW_50:
		return "Draw by the 50 move rule";
	case CS_DRAW_REPEAT:
		return "Draw by repetition";
	case CS_DRAW_MATERIAL:
		return "Draw: not enough material";
	default:
		return turn ? "Black to move" : "White to move";
	}
}

/* ---- notation -------------------------------------------------------- */

int chess_san(struct chess *g, const struct chess_move *m, char *out, size_t n)
{
	static const char letters[] = " PNBRQK";
	struct chess_move list[CH_MAX_MOVES];
	int piece = g->sq[m->from], t = CH_TYPE(piece);
	char buf[16];
	int len = 0, count, i;
	bool same_file = false, same_rank = false, other = false;

	if (m->flags & CF_CASTLE) {
		strlcpy(buf, m->to > m->from ? "O-O" : "O-O-O", sizeof(buf));
		len = (int)strlen(buf);
	} else {
		if (t != CH_PAWN) {
			buf[len++] = letters[t];
			count = gen_legal(g, list, false);
			for (i = 0; i < count; i++) {
				if (list[i].to != m->to || list[i].from == m->from ||
				    CH_TYPE(g->sq[list[i].from]) != t)
					continue;
				other = true;
				if (CH_FILE(list[i].from) == CH_FILE(m->from))
					same_file = true;
				if (CH_RANK(list[i].from) == CH_RANK(m->from))
					same_rank = true;
			}
			if (other) {
				if (!same_file)
					buf[len++] = (char)('a' + CH_FILE(m->from));
				else if (!same_rank)
					buf[len++] = (char)('1' + CH_RANK(m->from));
				else {
					buf[len++] = (char)('a' + CH_FILE(m->from));
					buf[len++] = (char)('1' + CH_RANK(m->from));
				}
			}
		} else if (m->flags & CF_CAPTURE) {
			buf[len++] = (char)('a' + CH_FILE(m->from));
		}
		if (m->flags & CF_CAPTURE)
			buf[len++] = 'x';
		buf[len++] = (char)('a' + CH_FILE(m->to));
		buf[len++] = (char)('1' + CH_RANK(m->to));
		if (m->flags & CF_PROMO) {
			buf[len++] = '=';
			buf[len++] = letters[m->promo];
		}
		buf[len] = '\0';
	}
	make(g, m);
	if (chess_in_check(g, g->turn)) {
		struct chess_move reply[CH_MAX_MOVES];

		buf[len++] = gen_legal(g, reply, false) ? '+' : '#';
		buf[len] = '\0';
	}
	unmake(g);
	return (int)strlcpy(out, buf, n);
}

/* ---- evaluation ------------------------------------------------------ */

static const s16 value[7] = { 0, 100, 320, 330, 500, 900, 0 };

/* piece-square tables, rank 8 first, from White's point of view */
static const s8 pst[7][64] = {
	{ 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 50, 50, 50, 50, 50, 50, 50, 50,
	  10, 10, 20, 30, 30, 20, 10, 10, 5, 5, 10, 25, 25, 10, 5, 5,
	  0, 0, 0, 20, 20, 0, 0, 0, 5, -5, -10, 0, 0, -10, -5, 5,
	  5, 10, 10, -20, -20, 10, 10, 5, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ -50, -40, -30, -30, -30, -30, -40, -50, -40, -20, 0, 0, 0, 0, -20, -40,
	  -30, 0, 10, 15, 15, 10, 0, -30, -30, 5, 15, 20, 20, 15, 5, -30,
	  -30, 0, 15, 20, 20, 15, 0, -30, -30, 5, 10, 15, 15, 10, 5, -30,
	  -40, -20, 0, 5, 5, 0, -20, -40, -50, -40, -30, -30, -30, -30, -40, -50 },
	{ -20, -10, -10, -10, -10, -10, -10, -20, -10, 0, 0, 0, 0, 0, 0, -10,
	  -10, 0, 5, 10, 10, 5, 0, -10, -10, 5, 5, 10, 10, 5, 5, -10,
	  -10, 0, 10, 10, 10, 10, 0, -10, -10, 10, 10, 10, 10, 10, 10, -10,
	  -10, 5, 0, 0, 0, 0, 5, -10, -20, -10, -10, -10, -10, -10, -10, -20 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 5, 10, 10, 10, 10, 10, 10, 5,
	  -5, 0, 0, 0, 0, 0, 0, -5, -5, 0, 0, 0, 0, 0, 0, -5,
	  -5, 0, 0, 0, 0, 0, 0, -5, -5, 0, 0, 0, 0, 0, 0, -5,
	  -5, 0, 0, 0, 0, 0, 0, -5, 0, 0, 0, 5, 5, 0, 0, 0 },
	{ -20, -10, -10, -5, -5, -10, -10, -20, -10, 0, 0, 0, 0, 0, 0, -10,
	  -10, 0, 5, 5, 5, 5, 0, -10, -5, 0, 5, 5, 5, 5, 0, -5,
	  0, 0, 5, 5, 5, 5, 0, -5, -10, 5, 5, 5, 5, 5, 0, -10,
	  -10, 0, 5, 0, 0, 0, 0, -10, -20, -10, -10, -5, -5, -10, -10, -20 },
	{ -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
	  -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
	  -20, -30, -30, -40, -40, -30, -30, -20, -10, -20, -20, -20, -20, -20, -20, -10,
	  20, 20, 0, 0, 0, 0, 20, 20, 20, 30, 10, 0, 0, 10, 30, 20 },
};

static const s8 king_end[64] = {
	-50, -40, -30, -20, -20, -30, -40, -50, -30, -20, -10, 0, 0, -10, -20, -30,
	-30, -10, 20, 30, 30, 20, -10, -30, -30, -10, 30, 40, 40, 30, -10, -30,
	-30, -10, 30, 40, 40, 30, -10, -30, -30, -10, 20, 30, 30, 20, -10, -30,
	-30, -30, 0, 0, 0, 0, -30, -30, -50, -30, -30, -30, -30, -30, -30, -50,
};

int chess_eval(const struct chess *g)
{
	int score = 0, i, material = 0;

	for (i = 0; i < 64; i++) {
		int p = g->sq[i];

		if (p && CH_TYPE(p) != CH_PAWN && CH_TYPE(p) != CH_KING)
			material += value[CH_TYPE(p)];
	}
	for (i = 0; i < 64; i++) {
		int p = g->sq[i], t, idx, v;

		if (!p)
			continue;
		t = CH_TYPE(p);
		/* tables are drawn rank 8 first: white mirrors the rank */
		idx = CH_COLOR(p) ? CH_RANK(i) * 8 + CH_FILE(i) :
				    (7 - CH_RANK(i)) * 8 + CH_FILE(i);
		v = value[t] + (t == CH_KING && material < 2600 ? king_end[idx] :
				pst[t][idx]);
		score += CH_COLOR(p) ? -v : v;
	}
	return g->turn ? -score : score;
}

/* ---- search ---------------------------------------------------------- */

static struct chess_move ply_moves[MAX_PLY][CH_MAX_MOVES];
static int ply_score[MAX_PLY][CH_MAX_MOVES];
static unsigned long nodes, node_limit;
static bool aborted, limited;
static u32 rng = 2463534242u;

void chess_seed(u32 seed)
{
	rng = seed ? seed : 2463534242u;
}

static u32 rnd(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return rng;
}

static int order_score(const struct chess *g, const struct chess_move *m)
{
	int s = 0;

	if (m->flags & CF_CAPTURE) {
		int victim = (m->flags & CF_EP) ? CH_PAWN : CH_TYPE(g->sq[m->to]);

		s = 10000 + value[victim] * 10 - CH_TYPE(g->sq[m->from]);
	}
	if (m->flags & CF_PROMO)
		s += 9000 + value[m->promo];
	return s;
}

/* selection step of a lazy sort: bring the best remaining move to @i */
static void pick(struct chess_move *mv, int *sc, int n, int i)
{
	int best = i, k;

	for (k = i + 1; k < n; k++)
		if (sc[k] > sc[best])
			best = k;
	if (best != i) {
		struct chess_move tm = mv[i];
		int ts = sc[i];

		mv[i] = mv[best];
		sc[i] = sc[best];
		mv[best] = tm;
		sc[best] = ts;
	}
}

static bool is_draw_in_search(const struct chess *g)
{
	int i, lowest = g->nhist - g->halfmove;

	if (g->halfmove >= 100)
		return true;
	if (lowest < 0)
		lowest = 0;
	for (i = g->nhist - 1; i >= lowest; i--)
		if (g->hist[i].hash == g->hash)
			return true;
	return insufficient_material(g);
}

static int quiesce(struct chess *g, int alpha, int beta, int ply)
{
	struct chess_move *mv = ply_moves[ply];
	int *sc = ply_score[ply];
	int stand = chess_eval(g), n, i;

	nodes++;
	if (limited && nodes > node_limit) {
		aborted = true;
		return 0;
	}
	if (ply >= MAX_PLY - 1)
		return stand;
	if (stand >= beta)
		return stand;
	if (stand > alpha)
		alpha = stand;
	n = gen_legal(g, mv, true);
	for (i = 0; i < n; i++)
		sc[i] = order_score(g, &mv[i]);
	for (i = 0; i < n; i++) {
		int v;

		pick(mv, sc, n, i);
		make(g, &mv[i]);
		v = -quiesce(g, -beta, -alpha, ply + 1);
		unmake(g);
		if (aborted)
			return 0;
		if (v >= beta)
			return v;
		if (v > alpha)
			alpha = v;
	}
	return alpha;
}

static int negamax(struct chess *g, int depth, int alpha, int beta, int ply)
{
	struct chess_move *mv = ply_moves[ply];
	int *sc = ply_score[ply];
	int n, i, best = -INF;
	bool check = chess_in_check(g, g->turn);

	nodes++;
	if (limited && nodes > node_limit) {
		aborted = true;
		return 0;
	}
	if (ply > 0 && is_draw_in_search(g))
		return 0;
	if (check && ply < MAX_PLY - 2)
		depth++;
	if (depth <= 0 || ply >= MAX_PLY - 1)
		return quiesce(g, alpha, beta, ply);
	n = gen_legal(g, mv, false);
	if (!n)
		return check ? -(MATE - ply) : 0;
	for (i = 0; i < n; i++)
		sc[i] = order_score(g, &mv[i]);
	for (i = 0; i < n; i++) {
		int v;

		pick(mv, sc, n, i);
		make(g, &mv[i]);
		v = -negamax(g, depth - 1, -beta, -alpha, ply + 1);
		unmake(g);
		if (aborted)
			return 0;
		if (v > best)
			best = v;
		if (v > alpha)
			alpha = v;
		if (alpha >= beta)
			break;
	}
	return best;
}

bool chess_best(struct chess *g, int depth, unsigned long max_nodes, int noise,
		struct chess_move *out)
{
	struct chess_move root[CH_MAX_MOVES];
	int rscore[CH_MAX_MOVES];
	int n = gen_legal(g, root, false), d, i, best_i = 0;

	if (!n)
		return false;
	*out = root[0];
	if (n == 1)
		return true;
	nodes = 0;
	node_limit = max_nodes;
	aborted = false;

	for (d = 1; d <= depth; d++) {
		int alpha = -INF, best = -INF, best_here = 0;
		bool full = noise && d == depth;

		limited = d > 1 && max_nodes;
		for (i = 0; i < n; i++)
			rscore[i] = order_score(g, &root[i]) + (i == best_i ? 100000 : 0);
		for (i = 0; i < n; i++) {
			int v;

			pick(root, rscore, n, i);
			make(g, &root[i]);
			v = -negamax(g, d - 1, -INF, full ? INF : -alpha, 1);
			unmake(g);
			if (aborted)
				break;
			rscore[i] = v;	/* keeps the order: the list is sorted in place */
			if (v > best) {
				best = v;
				best_here = i;
			}
			if (v > alpha && !full)
				alpha = v;
		}
		if (aborted)
			break;
		best_i = best_here;
		*out = root[best_i];
		if (best >= MATE - MAX_PLY)
			break;		/* a forced mate: no need to look deeper */
		if (full) {
			/* choose among moves within @noise of the best */
			int ok[CH_MAX_MOVES], cnt = 0;

			for (i = 0; i < n; i++)
				if (rscore[i] >= best - noise)
					ok[cnt++] = i;
			*out = root[ok[rnd() % (u32)cnt]];
		}
	}
	return true;
}

/* ---- perft ----------------------------------------------------------- */

static unsigned long perft_rec(struct chess *g, int depth, int ply)
{
	struct chess_move *mv = ply_moves[ply];
	unsigned long total = 0;
	int n, i;

	n = gen_legal(g, mv, false);
	if (depth == 1)
		return (unsigned long)n;
	for (i = 0; i < n; i++) {
		make(g, &mv[i]);
		total += perft_rec(g, depth - 1, ply + 1);
		unmake(g);
	}
	return total;
}

unsigned long chess_perft(struct chess *g, int depth)
{
	if (depth <= 0)
		return 1;
	if (depth >= MAX_PLY)
		return 0;
	return perft_rec(g, depth, 0);
}
