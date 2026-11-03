// SPDX-License-Identifier: GPL-2.0-only
/*
 * desk: Chess, drawn in real pixels (the desktop must run on the pixel
 * display). Click a piece and then a square, or drag it. The computer plays
 * the other side at one of four levels; Game/Undo takes moves back.
 *
 * The rules and the computer player are in chess_rules.c; this file is the
 * board, the pieces, the side panel and the mouse.
 */
#include <hix/kernel.h>
#include <desk/desk.h>
#include <desk/chess.h>

/* ---- layout (pixels inside the window) ------------------------------- */

#define CW		68			/* client size in cells */
#define CHH		20
#define PXW		(CW * 12)		/* 816 */
#define PXH		(CHH * 28)		/* 560 */
#define SQ		64
#define BX		32			/* the board */
#define BY		24
#define BAR_X		10			/* the evaluation bar */
#define BAR_W		12
#define PX0		568			/* the side panel */
#define PW		232
#define SMALL		24			/* captured pieces */

/* the look */
#define C_BG		0x312E2B
#define C_PANEL		0x262421
#define C_PANEL2	0x1E1C1A
#define C_TEXT		0xE8E6E3
#define C_DIM		0x989795
#define C_GREEN		0x81B64C
#define C_GREEN_DK	0x5E8C34
#define C_BTN		0x4B4845
#define C_LIGHT		0xEEEED2
#define C_DARK		0x769656
#define C_HL_LIGHT	0xF5F682
#define C_HL_DARK	0xBACA2B

enum { M_WHITE, M_BLACK, M_TWO };		/* who the human plays */
enum { CMD_NEW = 1, CMD_UNDO, CMD_FLIP, CMD_AS_WHITE, CMD_AS_BLACK, CMD_TWO,
	CMD_LEVEL0 };

static const struct {
	const char *name;
	int depth;
	unsigned long nodes;
	int noise;
} levels[4] = {
	{ "Easy", 1, 0, 90 },
	{ "Medium", 2, 20000, 20 },
	{ "Hard", 3, 70000, 0 },
	{ "Expert", 4, 220000, 0 },
};

/* ---- state (one window: APP_SINGLE) ---------------------------------- */

static u32 pixels[PXW * PXH];
static struct gfx_canvas canvas;

/* pieces baked with outline and shadow: [color][K Q R B N P] */
static u32 baked[2][6][SQ * SQ];
static u32 small[2][6][SMALL * SMALL];
static bool baked_ready;

static struct chess game;
static struct chess_move moves[CH_MAX_MOVES];
static int nmoves;
static char san[CH_MAX_HIST][10];

static struct {
	int mode, level;
	bool flip;
	int sel;			/* selected square or -1 */
	bool drag;
	int dpx, dpy;			/* pointer while dragging */
	int last_from, last_to;
	bool promo;			/* waiting for the promotion piece */
	int promo_from, promo_to;
	bool thinking;
	bool dirty;
	enum chess_status status;
	/* the slide of the piece that just moved */
	bool anim;
	int anim_from, anim_to;
	unsigned long anim_t0;
	/* clocks: milliseconds spent by white and black */
	unsigned long clock[2];
	unsigned long last_tick;
} ui;

#define ANIM_MS		170

static int sprite_index(int type)
{
	switch (type) {
	case CH_KING:
		return 0;
	case CH_QUEEN:
		return 1;
	case CH_ROOK:
		return 2;
	case CH_BISHOP:
		return 3;
	case CH_KNIGHT:
		return 4;
	default:
		return 5;
	}
}

/* ---- baking the pieces ---------------------------------------------- */

static void dilate(const u8 *in, u8 *out, int w, int h)
{
	int x, y, i, j;

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			u8 m = 0;

			for (j = -1; j <= 1; j++)
				for (i = -1; i <= 1; i++) {
					int xx = x + i, yy = y + j;

					if (xx >= 0 && yy >= 0 && xx < w && yy < h &&
					    in[yy * w + xx] > m)
						m = in[yy * w + xx];
				}
			out[y * w + x] = m;
		}
	}
}

/* @top over @under, both straight alpha (a, r, g, b as separate values) */
static u32 over(u32 top, u32 under)
{
	unsigned int ta = top >> 24, ua = under >> 24;
	unsigned int oa = ta + ua * (255 - ta) / 255;
	unsigned int r, g, b;

	if (!oa)
		return 0;
	r = (GFX_R(top) * ta * 255 + GFX_R(under) * ua * (255 - ta)) / (oa * 255);
	g = (GFX_G(top) * ta * 255 + GFX_G(under) * ua * (255 - ta)) / (oa * 255);
	b = (GFX_B(top) * ta * 255 + GFX_B(under) * ua * (255 - ta)) / (oa * 255);
	return (oa << 24) | (r << 16) | (g << 8) | b;
}

static void bake_all(void)
{
	static u8 d1[SQ * SQ], d2[SQ * SQ], shadow[SQ * SQ];
	int color, p, i, x, y;

	if (baked_ready)
		return;
	for (p = 0; p < 6; p++) {
		const u8 *a = &gfx_chess_sprites[p * SQ * SQ];

		dilate(a, d1, SQ, SQ);
		dilate(d1, d2, SQ, SQ);
		memset(shadow, 0, sizeof(shadow));
		for (y = 0; y < SQ - 3; y++)
			for (x = 0; x < SQ - 2; x++)
				shadow[(y + 3) * SQ + x + 2] = d1[y * SQ + x];
		for (color = 0; color < 2; color++) {
			u32 fill = color ? 0x5A5652 : 0xFAFAFA;
			u32 edge = color ? 0x0F0E0D : 0x2B2926;

			for (i = 0; i < SQ * SQ; i++) {
				u32 px = (u32)shadow[i] * 90 / 255 << 24;

				px = over(((u32)d2[i] << 24) | edge, px);
				px = over(((u32)a[i] << 24) | fill, px);
				baked[color][p][i] = px;
			}
			/* the small version: average 3x3 source pixels */
			for (y = 0; y < SMALL; y++)
				for (x = 0; x < SMALL; x++) {
					unsigned int sa = 0, sr = 0, sg = 0, sb = 0, n = 0;
					int i2, j2;

					for (j2 = 0; j2 < 3; j2++)
						for (i2 = 0; i2 < 3; i2++) {
							int sx = x * SQ / SMALL + i2 - 0;
							int sy = y * SQ / SMALL + j2 - 0;
							u32 q;

							if (sx >= SQ || sy >= SQ)
								continue;
							q = baked[color][p][sy * SQ + sx];
							sa += q >> 24;
							sr += GFX_R(q) * (q >> 24);
							sg += GFX_G(q) * (q >> 24);
							sb += GFX_B(q) * (q >> 24);
							n++;
						}
					if (!n || !sa) {
						small[color][p][y * SMALL + x] = 0;
					} else {
						small[color][p][y * SMALL + x] =
							((sa / n) << 24) | ((sr / sa) << 16) |
							((sg / sa) << 8) | (sb / sa);
					}
				}
		}
	}
	baked_ready = true;
}

/* ---- geometry -------------------------------------------------------- */

static void sq_xy(int sq, int *x, int *y)
{
	int col = ui.flip ? 7 - CH_FILE(sq) : CH_FILE(sq);
	int row = ui.flip ? CH_RANK(sq) : 7 - CH_RANK(sq);

	*x = BX + col * SQ;
	*y = BY + row * SQ;
}

static int xy_sq(int px, int py)
{
	int col = (px - BX) / SQ, row = (py - BY) / SQ;

	if (px < BX || py < BY || col > 7 || row > 7)
		return -1;
	return ui.flip ? CH_SQ(7 - col, row) : CH_SQ(col, 7 - row);
}

static bool is_light(int sq)
{
	return ((CH_FILE(sq) + CH_RANK(sq)) & 1) != 0;
}

static bool human_turn(void)
{
	return ui.mode == M_TWO ||
	       game.turn == (ui.mode == M_WHITE ? CH_WHITE_SIDE : CH_BLACK_SIDE);
}

static bool legal_to(int from, int to)
{
	int i;

	for (i = 0; i < nmoves; i++)
		if (moves[i].from == from && moves[i].to == to)
			return true;
	return false;
}

/* ---- the game -------------------------------------------------------- */

static void refresh_position(void)
{
	nmoves = chess_moves(&game, moves);
	ui.status = chess_status(&game);
	ui.sel = -1;
	ui.dirty = true;
	if (game.nhist) {
		ui.last_from = game.hist[game.nhist - 1].m.from;
		ui.last_to = game.hist[game.nhist - 1].m.to;
	} else {
		ui.last_from = ui.last_to = -1;
	}
}

static void new_game(void)
{
	chess_init(&game);
	ui.sel = -1;
	ui.drag = false;
	ui.promo = false;
	ui.thinking = false;
	ui.anim = false;
	ui.clock[0] = ui.clock[1] = 0;
	ui.flip = ui.mode == M_BLACK;
	refresh_position();
}

static void start_anim(int from, int to)
{
	ui.anim = true;
	ui.anim_from = from;
	ui.anim_to = to;
	ui.anim_t0 = desk_now();
}

static void make_move(const struct chess_move *m, bool animate)
{
	int ply = game.nhist;

	if (ply >= CH_MAX_HIST - 40)
		return;
	chess_san(&game, m, san[ply], sizeof(san[ply]));
	if (!chess_play(&game, m))
		return;
	refresh_position();
	if (animate)
		start_anim(m->from, m->to);
}

/* a pawn reaching the last rank: ask which piece */
static bool needs_promotion(int from, int to)
{
	int i;

	for (i = 0; i < nmoves; i++)
		if (moves[i].from == from && moves[i].to == to &&
		    (moves[i].flags & CF_PROMO))
			return true;
	return false;
}

static void try_move(int from, int to, bool animate)
{
	struct chess_move m;

	if (!legal_to(from, to))
		return;
	if (needs_promotion(from, to)) {
		ui.promo = true;
		ui.promo_from = from;
		ui.promo_to = to;
		ui.sel = -1;
		ui.dirty = true;
		return;
	}
	if (chess_find_move(&game, from, to, 0, &m))
		make_move(&m, animate);
}

static void undo_move(void)
{
	int n = ui.mode == M_TWO ? 1 : 2;

	ui.thinking = false;
	ui.anim = false;
	ui.promo = false;
	/* with the computer, take back to the human's turn */
	if (ui.mode != M_TWO && game.nhist == 1 && game.turn != (ui.mode == M_WHITE ? 0 : 1))
		n = 1;
	while (n-- > 0 && game.nhist > 0)
		chess_undo(&game);
	refresh_position();
}

/* ---- drawing --------------------------------------------------------- */

static void text_r(const struct gfx_font *f, int right, int y, const char *s,
		   u32 color)
{
	gfx_text(&canvas, f, right - gfx_text_width(f, s), y, s, color);
}

static void draw_piece_at(int piece, int x, int y)
{
	gfx_blit_argb(&canvas, x, y, baked[CH_COLOR(piece)][sprite_index(CH_TYPE(piece))],
		      SQ, SQ);
}

static void draw_board(void)
{
	int sq, i;

	for (sq = 0; sq < 64; sq++) {
		int x, y;
		bool light = is_light(sq);
		u32 col = light ? C_LIGHT : C_DARK;

		sq_xy(sq, &x, &y);
		if (sq == ui.last_from || sq == ui.last_to)
			col = light ? C_HL_LIGHT : C_HL_DARK;
		if (sq == ui.sel)
			col = light ? 0xF7F769 : 0xBDCB35;
		gfx_fill(&canvas, x, y, SQ, SQ, col);
	}
	/* a king in check glows red */
	if (chess_in_check(&game, game.turn)) {
		int k;

		for (k = 0; k < 64; k++)
			if (game.sq[k] == (CH_KING | (game.turn ? CH_BLACK : 0))) {
				int x, y;

				sq_xy(k, &x, &y);
				for (i = 0; i < 6; i++)
					gfx_disc16(&canvas, (x + SQ / 2) * 16, (y + SQ / 2) * 16,
						   (31 - i * 4) * 16, 0xE8332B, 55);
			}
	}
	/* coordinates in the corners, coloured like the other kind of square */
	for (i = 0; i < 8; i++) {
		char c[2] = { 0, 0 };
		int file = ui.flip ? 7 - i : i;
		int rank = ui.flip ? i : 7 - i;
		int x, y;

		sq_xy(CH_SQ(file, ui.flip ? 7 : 0), &x, &y);
		c[0] = (char)('a' + file);
		gfx_text(&canvas, &gfx_font_ui, x + SQ - 11, y + SQ - 19, c,
			 is_light(CH_SQ(file, ui.flip ? 7 : 0)) ? C_DARK : C_LIGHT);
		sq_xy(CH_SQ(ui.flip ? 7 : 0, rank), &x, &y);
		c[0] = (char)('1' + rank);
		gfx_text(&canvas, &gfx_font_ui, x + 3, y + 1, c,
			 is_light(CH_SQ(ui.flip ? 7 : 0, rank)) ? C_DARK : C_LIGHT);
	}
	/* legal moves of the selected piece: dots, and rings for captures */
	if (ui.sel >= 0) {
		for (i = 0; i < nmoves; i++) {
			int x, y;

			if (moves[i].from != ui.sel)
				continue;
			sq_xy(moves[i].to, &x, &y);
			if (moves[i].flags & CF_CAPTURE)
				gfx_ring16(&canvas, (x + SQ / 2) * 16, (y + SQ / 2) * 16,
					   30 * 16, 6 * 16, 0x000000, 38);
			else
				gfx_disc16(&canvas, (x + SQ / 2) * 16, (y + SQ / 2) * 16,
					   10 * 16, 0x000000, 38);
		}
	}
	/* the pieces */
	for (sq = 0; sq < 64; sq++) {
		int x, y;

		if (!game.sq[sq])
			continue;
		if (ui.drag && sq == ui.sel)
			continue;
		if (ui.anim && sq == ui.anim_to)
			continue;
		sq_xy(sq, &x, &y);
		draw_piece_at(game.sq[sq], x, y);
	}
	if (ui.anim) {
		unsigned long t = desk_now() - ui.anim_t0;
		int x0, y0, x1, y1, f = t >= ANIM_MS ? 256 : (int)(t * 256 / ANIM_MS);

		f = f * (512 - f) / 256;		/* ease out */
		sq_xy(ui.anim_from, &x0, &y0);
		sq_xy(ui.anim_to, &x1, &y1);
		draw_piece_at(game.sq[ui.anim_to], x0 + (x1 - x0) * f / 256,
			      y0 + (y1 - y0) * f / 256);
	}
	if (ui.drag && ui.sel >= 0)
		draw_piece_at(game.sq[ui.sel], ui.dpx - SQ / 2, ui.dpy - SQ / 2);
	/* the promotion chooser: a column of four on the target file */
	if (ui.promo) {
		static const int choices[4] = { CH_QUEEN, CH_KNIGHT, CH_ROOK, CH_BISHOP };
		int x, y, dir;

		sq_xy(ui.promo_to, &x, &y);
		/* from the edge of the board towards the middle */
		dir = y < BY + 4 * SQ ? 1 : -1;
		gfx_rrect(&canvas, x - 2, (dir > 0 ? y : y - 3 * SQ) - 2, SQ + 4,
			  4 * SQ + 4, 6, 0xF2F2F2, 255);
		for (i = 0; i < 4; i++) {
			int py = y + dir * i * SQ;
			int color = CH_COLOR(game.sq[ui.promo_from]);

			gfx_rrect(&canvas, x + 2, py + 2, SQ - 4, SQ - 4, 6, 0xDADADA, 255);
			draw_piece_at(choices[i] | (color ? CH_BLACK : 0), x, py);
		}
	}
}

static void draw_eval_bar(void)
{
	int e = chess_eval(&game), h = 8 * SQ, white, top;

	if (game.turn)
		e = -e;				/* from white's point of view */
	if (ui.status == CS_CHECKMATE)
		e = game.turn ? 1000 : -1000;
	e = e < -1000 ? -1000 : e > 1000 ? 1000 : e;
	white = h / 2 + e * (h / 2) / 1000;
	top = ui.flip ? white : h - white;	/* where the white part starts */
	gfx_rrect(&canvas, BAR_X, BY, BAR_W, h, 4, 0x403D39, 255);
	if (ui.flip)
		gfx_rrect(&canvas, BAR_X, BY, BAR_W, white, 4, 0xF0F0F0, 255);
	else
		gfx_rrect(&canvas, BAR_X, BY + top, BAR_W, white, 4, 0xF0F0F0, 255);
	gfx_fill(&canvas, BAR_X, BY + h / 2 - 1, BAR_W, 2, 0x8A8784);
}

static void fmt_clock(unsigned long ms, char *out, size_t n)
{
	unsigned long s = ms / 1000;

	snprintf(out, n, "%lu:%02lu", s / 60, s % 60);
}

/* pieces the side @by has taken, as small sprites, with the material lead */
static void draw_captured(int by, int x, int y)
{
	static const int order[5] = { CH_QUEEN, CH_ROOK, CH_BISHOP, CH_KNIGHT, CH_PAWN };
	static const int start[7] = { 0, 8, 2, 2, 2, 1, 0 };
	static const int val[7] = { 0, 1, 3, 3, 5, 9, 0 };
	int have[2][7] = { { 0 } }, i, k, lead = 0, cx = x;

	for (i = 0; i < 64; i++)
		if (game.sq[i])
			have[CH_COLOR(game.sq[i])][CH_TYPE(game.sq[i])]++;
	for (i = 0; i < 7; i++)
		lead += val[i] * (have[by][i] - have[!by][i]);
	for (i = 0; i < 5; i++) {
		int t = order[i], lost = start[t] - have[!by][t];

		for (k = 0; k < lost; k++) {
			gfx_blit_argb(&canvas, cx, y, small[!by][sprite_index(t)], SMALL, SMALL);
			cx += t == CH_PAWN ? 9 : 14;
		}
		cx += 4;
	}
	if (lead > 0) {
		char buf[8];

		snprintf(buf, sizeof(buf), "+%d", lead);
		gfx_text(&canvas, &gfx_font_ui, cx + 2, y + 3, buf, C_DIM);
	}
}

static void draw_card(int side, int y)
{
	char clk[16], name[24];
	bool human = ui.mode == M_TWO || side == (ui.mode == M_WHITE ? 0 : 1);
	bool to_move = game.turn == side && ui.status == CS_ONGOING;

	gfx_rrect(&canvas, PX0, y, PW, 56, 6, C_PANEL, 255);
	gfx_rrect(&canvas, PX0 + 8, y + 8, 40, 40, 4, side ? 0x403D39 : 0xE9E9E9, 255);
	gfx_blit_argb(&canvas, PX0 + 16, y + 16, small[side][0], SMALL, SMALL);
	if (ui.mode == M_TWO)
		snprintf(name, sizeof(name), side ? "Black" : "White");
	else if (human)
		snprintf(name, sizeof(name), "You");
	else
		snprintf(name, sizeof(name), "Computer");
	gfx_text(&canvas, &gfx_font_uib, PX0 + 58, y + 6, name, C_TEXT);
	if (ui.mode != M_TWO && !human)
		gfx_text(&canvas, &gfx_font_ui, PX0 + 58, y + 32, levels[ui.level].name, C_DIM);
	else
		gfx_text(&canvas, &gfx_font_ui, PX0 + 58, y + 32,
			 side ? "plays Black" : "plays White", C_DIM);
	fmt_clock(ui.clock[side], clk, sizeof(clk));
	gfx_rrect(&canvas, PX0 + PW - 58, y + 12, 50, 32, 4, to_move ? 0xFFFFFF : C_PANEL2, 255);
	text_r(&gfx_font_ui, PX0 + PW - 15, y + 17, clk, to_move ? 0x262421 : C_DIM);
}

static void draw_moves(void)
{
	int rows = 9, first, i, y = 100;
	int total = (game.nhist + 1) / 2;

	gfx_rrect(&canvas, PX0, y - 4, PW, rows * 24 + 8, 6, C_PANEL2, 255);
	first = total > rows ? total - rows : 0;
	for (i = first; i < total; i++) {
		char num[8];
		int ry = y + (i - first) * 24;

		if (i & 1)
			gfx_fill(&canvas, PX0 + 4, ry, PW - 8, 24, 0x23211F);
		snprintf(num, sizeof(num), "%d.", i + 1);
		text_r(&gfx_font_ui, PX0 + 38, ry + 3, num, C_DIM);
		gfx_text(&canvas, &gfx_font_ui, PX0 + 50, ry + 3, san[2 * i], C_TEXT);
		if (2 * i + 1 < game.nhist)
			gfx_text(&canvas, &gfx_font_ui, PX0 + 130, ry + 3, san[2 * i + 1], C_TEXT);
	}
	if (!game.nhist)
		gfx_text_center(&canvas, &gfx_font_ui, PX0, y + 100, PW, "No moves yet", C_DIM);
}

static void draw_button(int x, int y, int w, const char *label, bool primary)
{
	gfx_rrect(&canvas, x, y + 3, w, 36, 6, primary ? C_GREEN_DK : 0x3A3836, 255);
	gfx_rrect(&canvas, x, y, w, 36, 6, primary ? C_GREEN : C_BTN, 255);
	gfx_text_center(&canvas, &gfx_font_ui, x, y + 9, w, label, primary ? 0xFFFFFF : C_TEXT);
}

#define BTN_Y		490
static const struct { int x, w; } btn[3] = { { PX0, 104 }, { PX0 + 108, 60 }, { PX0 + 172, 60 } };

static struct win *cur_win;

static void draw_all(void)
{
	const char *msg;
	char buf[40];
	int top = ui.flip ? 0 : 1;

	gfx_clip_reset(&canvas);
	gfx_fill(&canvas, 0, 0, PXW, PXH, C_BG);
	draw_eval_bar();
	draw_board();

	draw_card(top, 10);
	draw_captured(top, PX0 + 4, 70);
	draw_moves();
	if (ui.thinking)
		msg = "Computer is thinking...";
	else if (ui.status == CS_ONGOING && chess_in_check(&game, game.turn))
		msg = game.turn ? "Check - Black to move" : "Check - White to move";
	else
		msg = chess_status_text(ui.status, game.turn);
	snprintf(buf, sizeof(buf), "%s", msg);
	gfx_text_center(&canvas, &gfx_font_uib, PX0, 354, PW, buf,
			ui.status == CS_ONGOING ? C_TEXT : 0xF5C542);
	draw_captured(!top, PX0 + 4, 388);
	draw_card(!top, 416);
	draw_button(btn[0].x, BTN_Y, btn[0].w, "New Game", true);
	draw_button(btn[1].x, BTN_Y, btn[1].w, "Undo", false);
	draw_button(btn[2].x, BTN_Y, btn[2].w, "Flip", false);
	desk_px_dirty(cur_win, 0, 0, PXW, PXH);
}

/* ---- the program ----------------------------------------------------- */

static void pointer(const struct win *w, int *x, int *y)
{
	if (!desk_pointer_px(w, x, y))
		*x = *y = -1;
}

static bool in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
	return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void pick_promotion(int px, int py)
{
	static const int choices[4] = { CH_QUEEN, CH_KNIGHT, CH_ROOK, CH_BISHOP };
	int x, y, dir, i;
	struct chess_move m;

	sq_xy(ui.promo_to, &x, &y);
	dir = y < BY + 4 * SQ ? 1 : -1;
	for (i = 0; i < 4; i++) {
		if (!in_rect(px, py, x, y + dir * i * SQ, SQ, SQ))
			continue;
		ui.promo = false;
		if (chess_find_move(&game, ui.promo_from, ui.promo_to, choices[i], &m))
			make_move(&m, true);
		return;
	}
	ui.promo = false;			/* a click elsewhere cancels */
	ui.dirty = true;
}

static void chess_mouse(struct win *w, int ev, int cx, int cy, int buttons)
{
	int px, py, s;

	(void)cx;
	(void)cy;
	pointer(w, &px, &py);
	if (px < 0 || !w->pix)
		return;
	if (ev == MEV_MOVE || ev == MEV_UP) {
		if (ui.drag) {
			ui.dpx = px;
			ui.dpy = py;
			ui.dirty = true;
		}
		if (ev == MEV_MOVE)
			return;
	}
	if (ev == MEV_UP) {
		if (!ui.drag)
			return;
		ui.drag = false;
		s = xy_sq(px, py);
		if (ui.sel >= 0 && s >= 0 && s != ui.sel && legal_to(ui.sel, s))
			try_move(ui.sel, s, false);
		else if (s != ui.sel)
			ui.sel = -1;		/* dropped on nothing useful */
		ui.dirty = true;
		return;
	}
	if (ev != MEV_DOWN || !(buttons & MOUSE_LEFT))
		return;

	if (ui.promo) {
		pick_promotion(px, py);
		return;
	}
	if (in_rect(px, py, btn[0].x, BTN_Y, btn[0].w, 36)) {
		new_game();
		return;
	}
	if (in_rect(px, py, btn[1].x, BTN_Y, btn[1].w, 36)) {
		undo_move();
		return;
	}
	if (in_rect(px, py, btn[2].x, BTN_Y, btn[2].w, 36)) {
		ui.flip = !ui.flip;
		ui.dirty = true;
		return;
	}
	s = xy_sq(px, py);
	if (s < 0 || ui.anim || ui.thinking)
		return;
	if (ui.status != CS_ONGOING || !human_turn())
		return;
	if (ui.sel >= 0 && legal_to(ui.sel, s)) {
		try_move(ui.sel, s, true);
		return;
	}
	if (game.sq[s] && CH_COLOR(game.sq[s]) == game.turn) {
		ui.sel = s;
		ui.drag = true;
		ui.dpx = px;
		ui.dpy = py;
	} else {
		ui.sel = -1;
	}
	ui.dirty = true;
}

static bool chess_key(struct win *w, int key, u8 mods)
{
	(void)w;
	(void)mods;
	switch (key) {
	case KEY_F(2):
	case 'n':
		new_game();
		return true;
	case 'u':
	case KEY_F(3):
		undo_move();
		return true;
	case 'f':
	case KEY_F(4):
		ui.flip = !ui.flip;
		ui.dirty = true;
		return true;
	case 27:
		ui.sel = -1;
		ui.drag = false;
		ui.promo = false;
		ui.dirty = true;
		return true;
	default:
		return false;
	}
}

static void chess_tick(struct win *w)
{
	unsigned long now = desk_now();
	struct chess_move m;

	(void)w;
	if (ui.last_tick && game.nhist && ui.status == CS_ONGOING)
		ui.clock[game.turn] += now - ui.last_tick;
	if (ui.last_tick && game.nhist && ui.status == CS_ONGOING &&
	    now / 1000 != ui.last_tick / 1000)
		ui.dirty = true;		/* the clock moved on a second */
	ui.last_tick = now;

	if (ui.anim) {
		if (now - ui.anim_t0 >= ANIM_MS)
			ui.anim = false;
		ui.dirty = true;
		return;
	}
	if (ui.status != CS_ONGOING || ui.promo || human_turn())
		return;
	if (!ui.thinking) {			/* paint "thinking" first */
		ui.thinking = true;
		ui.dirty = true;
		return;
	}
	ui.thinking = false;
	if (chess_best(&game, levels[ui.level].depth, levels[ui.level].nodes,
		       levels[ui.level].noise, &m))
		make_move(&m, true);
	ui.dirty = true;
}

static void chess_command(struct win *w, int id)
{
	(void)w;
	switch (id) {
	case CMD_NEW:
		new_game();
		break;
	case CMD_UNDO:
		undo_move();
		break;
	case CMD_FLIP:
		ui.flip = !ui.flip;
		ui.dirty = true;
		break;
	case CMD_AS_WHITE:
	case CMD_AS_BLACK:
	case CMD_TWO:
		ui.mode = id == CMD_AS_WHITE ? M_WHITE : id == CMD_AS_BLACK ? M_BLACK : M_TWO;
		new_game();
		break;
	default:
		if (id >= CMD_LEVEL0 && id < CMD_LEVEL0 + 4) {
			ui.level = id - CMD_LEVEL0;
			ui.dirty = true;
		}
		break;
	}
}

static void chess_open(struct win *w, const char *arg)
{
	(void)arg;
	cur_win = w;
	ui.mode = M_WHITE;
	ui.level = 1;
	ui.flip = false;
	ui.last_tick = 0;
	if (desk_pixels()) {
		bake_all();
		gfx_canvas_init(&canvas, PXW, PXH, pixels);
		w->pix = &canvas;
	}
	chess_seed(desk_rand());
	new_game();
}

static void chess_paint(struct win *w, struct steel_surface *c)
{
	const u8 attr = STEEL_ATTR(STEEL_BLACK, STEEL_WHITE);

	if (!w->pix) {
		steel_puts(c, 2, 2, "Chess is drawn in pixels.", attr);
		steel_puts(c, 2, 4, "Start the desktop on the pixel display", attr);
		steel_puts(c, 2, 5, "(QEMU's default -vga std provides it).", attr);
		return;
	}
	if (ui.dirty) {
		ui.dirty = false;
		draw_all();
	}
}

static const struct menu_item game_items[] = {
	{ "New game", CMD_NEW, "F2" },
	{ "Undo", CMD_UNDO, "F3" },
	{ "Flip board", CMD_FLIP, "F4" },
	{ "", 0, NULL },
	{ "Exit", CMD_CLOSE, "Alt+F4" },
};
static const struct menu_item play_items[] = {
	{ "You play White", CMD_AS_WHITE, NULL },
	{ "You play Black", CMD_AS_BLACK, NULL },
	{ "Two players", CMD_TWO, NULL },
	{ "", 0, NULL },
	{ "Level: Easy", CMD_LEVEL0, NULL },
	{ "Level: Medium", CMD_LEVEL0 + 1, NULL },
	{ "Level: Hard", CMD_LEVEL0 + 2, NULL },
	{ "Level: Expert", CMD_LEVEL0 + 3, NULL },
};
static const struct menu menus[] = {
	{ "Game", game_items, ARRAY_SIZE(game_items) },
	{ "Play", play_items, ARRAY_SIZE(play_items) },
	DESK_HELP_MENU,
};

const struct app app_chess = {
	.name = "chess",
	.title = "Chess",
	.icon = { "\xDC\xDB\xDC\xDB\xDC\xDB", "\xDB\xDC\xDB\xDC\xDB\xDC" },
	.icon_attr = { STEEL_ATTR(STEEL_WHITE, STEEL_BLACK),
		       STEEL_ATTR(STEEL_WHITE, STEEL_BLACK) },
	.cw = CW, .ch = CHH,
	.flags = APP_SINGLE,
	.tick_ms = 40,
	.menus = menus, .nmenus = ARRAY_SIZE(menus),
	.open = chess_open,
	.paint = chess_paint,
	.key = chess_key,
	.mouse = chess_mouse,
	.tick = chess_tick,
	.command = chess_command,
};
