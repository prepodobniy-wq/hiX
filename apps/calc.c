// SPDX-License-Identifier: GPL-2.0-only
/*
 * desk: Calculator. Fixed point with three decimals in a 32-bit integer
 * (the kernel has no floating point unit): up to 2,147,483.647.
 * Works with the mouse and with the keyboard: digits, + - * /, = or Enter,
 * C clears, Backspace erases a digit, N changes the sign.
 */
#include <hix/kernel.h>
#include <desk/desk.h>

#define SCALE	1000
#define LIMIT	0x7FFFFFFFLL

/* n / d for a 64-bit n without pulling in libgcc */
static u64 div64_32(u64 n, u32 d)
{
#if defined(__i386__) && !defined(HIX_HOST_TEST)
	u32 hi = (u32)(n >> 32), lo = (u32)n, qh = hi / d, rem = hi % d, ql;

	__asm__("divl %4" : "=a"(ql), "=d"(rem) : "a"(lo), "d"(rem), "r"(d));
	return ((u64)qh << 32) | ql;
#else
	return n / d;
#endif
}

struct calc {
	s32 acc;		/* left operand */
	char op;		/* pending operator or 0 */
	u32 mag;		/* number being typed, scaled */
	bool neg;
	bool typing;		/* digits are being entered */
	int frac;		/* decimals typed so far, -1 = no point yet */
	bool error;
	bool fresh;		/* the display shows a result */
};

static const char *const keys[5][4] = {
	{ "C", "CE", "<-", "/" },
	{ "7", "8", "9", "*" },
	{ "4", "5", "6", "-" },
	{ "1", "2", "3", "+" },
	{ "+/-", "0", ".", "=" },
};

static void calc_reset(struct calc *k)
{
	memset(k, 0, sizeof(*k));
	k->frac = -1;
}

static s32 entered(const struct calc *k)
{
	return k->neg ? -(s32)k->mag : (s32)k->mag;
}

/* a op b with overflow and zero checks; false means error */
static bool apply(s32 a, char op, s32 b, s32 *out)
{
	bool neg = (a < 0) != (b < 0);
	u64 ua = a < 0 ? (u64)-(s64)a : (u64)a;
	u64 ub = b < 0 ? (u64)-(s64)b : (u64)b;
	s64 r;

	switch (op) {
	case '+':
		r = (s64)a + b;
		break;
	case '-':
		r = (s64)a - b;
		break;
	case '*':
		ua = div64_32(ua * ub + SCALE / 2, SCALE);
		r = neg ? -(s64)ua : (s64)ua;
		break;
	case '/':
		if (!ub)
			return false;
		ua = div64_32(ua * SCALE + ub / 2, (u32)ub);
		r = neg ? -(s64)ua : (s64)ua;
		break;
	default:
		r = b;
		break;
	}
	if (r > LIMIT || r < -LIMIT)
		return false;
	*out = (s32)r;
	return true;
}

/* make the display show the accumulator */
static void show_acc(struct calc *k)
{
	k->mag = k->acc < 0 ? (u32)-k->acc : (u32)k->acc;
	k->neg = k->acc < 0;
}

static void commit(struct calc *k)
{
	s32 v = entered(k);

	if (k->op && !k->fresh && !apply(k->acc, k->op, v, &v)) {
		k->error = true;
		return;
	}
	k->acc = v;
	k->typing = false;
	show_acc(k);
}

static void begin_entry(struct calc *k)
{
	k->mag = 0;
	k->neg = false;
	k->frac = -1;
	k->typing = true;
	k->fresh = false;
}

static void press_digit(struct calc *k, int d)
{
	static const u32 place[3] = { 100, 10, 1 };

	if (k->error)
		return;
	if (!k->typing)
		begin_entry(k);
	if (k->frac < 0) {
		u64 v = (u64)k->mag * 10 + (u64)d * SCALE;

		if (v <= (u64)LIMIT)
			k->mag = (u32)v;
	} else if (k->frac < 3) {
		u64 v = (u64)k->mag + (u64)d * place[k->frac];

		if (v <= (u64)LIMIT) {
			k->mag = (u32)v;
			k->frac++;
		}
	}
}

static void press_op(struct calc *k, char op)
{
	if (k->error)
		return;
	if (k->typing || !k->op)
		commit(k);
	k->op = op;
	k->fresh = true;
}

static void press_equals(struct calc *k)
{
	if (k->error)
		return;
	commit(k);
	k->op = 0;
	k->fresh = true;
}

static void press_backspace(struct calc *k)
{
	static const u32 unit[4] = { 1000, 100, 10, 1 };

	if (k->error || !k->typing)
		return;
	if (k->frac < 0) {
		k->mag = k->mag / (10 * SCALE) * SCALE;
	} else if (k->frac == 0) {
		k->frac = -1;
	} else {
		k->frac--;
		k->mag = k->mag / unit[k->frac] * unit[k->frac];
	}
}

static void press(struct calc *k, const char *key)
{
	if (!strcmp(key, "C")) {
		calc_reset(k);
	} else if (!strcmp(key, "CE")) {
		if (k->error)
			calc_reset(k);
		begin_entry(k);
	} else if (!strcmp(key, "<-")) {
		press_backspace(k);
	} else if (!strcmp(key, "+/-")) {
		if (k->error)
			return;
		if (k->typing) {
			k->neg = !k->neg;
		} else {
			k->acc = -k->acc;
			show_acc(k);
		}
	} else if (!strcmp(key, ".")) {
		if (k->error)
			return;
		if (!k->typing)
			begin_entry(k);
		if (k->frac < 0)
			k->frac = 0;
	} else if (!strcmp(key, "=")) {
		press_equals(k);
	} else if (key[1] == '\0' && key[0] >= '0' && key[0] <= '9') {
		press_digit(k, key[0] - '0');
	} else if (key[1] == '\0') {
		press_op(k, key[0]);
	}
}

static void format(const struct calc *k, char *out, size_t size)
{
	char frac[8];
	int fl;
	bool neg = k->neg && k->mag;

	if (k->error) {
		strlcpy(out, "E", size);
		return;
	}
	snprintf(frac, sizeof(frac), "%03u", k->mag % SCALE);
	if (k->typing) {		/* show exactly what was typed */
		fl = k->frac < 0 ? 0 : k->frac;
	} else {			/* a result: drop trailing zeros */
		fl = 3;
		while (fl > 0 && frac[fl - 1] == '0')
			fl--;
	}
	frac[fl] = '\0';
	if (fl || (k->typing && k->frac == 0))
		snprintf(out, size, "%s%u.%s", neg ? "-" : "", k->mag / SCALE,
			 frac);
	else
		snprintf(out, size, "%s%u", neg ? "-" : "", k->mag / SCALE);
}

/* ---- the window ------------------------------------------------------ */

#define BTN_W	6
#define BTN_X	1
#define BTN_Y	3
#define BTN_STEP_Y 2

static void calc_open(struct win *w, const char *arg)
{
	(void)arg;
	calc_reset(DESK_PRIV(w, struct calc));
}

static void calc_paint(struct win *w, struct steel_surface *c)
{
	struct calc *k = DESK_PRIV(w, struct calc);
	const u8 gray = STEEL_ATTR(STEEL_BLACK, STEEL_LGRAY);
	char text[24];
	int r, col, len;

	steel_fill(c, ' ', gray);
	desk_draw_sunken(c, 1, 1, c->w - 2, 1);
	format(k, text, sizeof(text));
	len = (int)strlen(text);
	steel_puts(c, c->w - 2 - len, 1, text, A_WIN);
	if (k->op)
		steel_put(c, 2, 1, (u8)k->op, A_DIM);
	for (r = 0; r < 5; r++) {
		for (col = 0; col < 4; col++) {
			const char *label = keys[r][col];
			int bx = BTN_X + col * BTN_W;
			int by = BTN_Y + r * BTN_STEP_Y;
			int ll = (int)strlen(label);
			u8 attr = (col == 3 || r == 0) ?
				  STEEL_ATTR(STEEL_RED, STEEL_WHITE) : A_WIN;

			steel_rect(c, bx, by, BTN_W - 1, 1, ' ', attr);
			steel_puts(c, bx + (BTN_W - 1 - ll) / 2, by, label, attr);
		}
	}
}

static bool calc_key(struct win *w, int key, u8 mods)
{
	struct calc *k = DESK_PRIV(w, struct calc);
	char s[2] = { (char)key, '\0' };

	(void)mods;
	if ((key >= '0' && key <= '9') || key == '+' || key == '-' ||
	    key == '*' || key == '/' || key == '.') {
		press(k, s);
	} else if (key == '=' || key == '\n') {
		press(k, "=");
	} else if (key == 'c' || key == 'C' || key == 27) {
		press(k, "C");
	} else if (key == 8 || key == 127) {
		press(k, "<-");
	} else if (key == 'n' || key == 'N') {
		press(k, "+/-");
	} else {
		return false;
	}
	return true;
}

static void calc_mouse(struct win *w, int ev, int x, int y, int buttons)
{
	struct calc *k = DESK_PRIV(w, struct calc);
	int r = (y - BTN_Y) / BTN_STEP_Y, col = (x - BTN_X) / BTN_W;

	if (ev != MEV_DOWN || !(buttons & MOUSE_LEFT) || x < BTN_X || y < BTN_Y)
		return;
	if ((y - BTN_Y) % BTN_STEP_Y || (x - BTN_X) % BTN_W == BTN_W - 1)
		return;
	if (r < 5 && col < 4)
		press(k, keys[r][col]);
}

const struct app app_calc = {
	.name = "calc",
	.title = "Calculator",
	.icon = { "\xDA\xC4\xC4\xC4\xC4\xBF", "\xB3+-*/\xB3" },
	.icon_attr = { STEEL_ATTR(STEEL_BLACK, STEEL_WHITE),
		       STEEL_ATTR(STEEL_RED, STEEL_WHITE) },
	.cw = 26, .ch = 12,
	.flags = APP_SINGLE,
	.open = calc_open,
	.paint = calc_paint,
	.key = calc_key,
	.mouse = calc_mouse,
};
