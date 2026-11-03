// SPDX-License-Identifier: GPL-2.0-only
/*
 * steel: the text console.
 *
 * 80x25 live screen plus a scrollback ring of HIST_LINES lines. Understands
 * \n \r \b \t \f and a small ANSI subset: ESC[<n>;<n>m (colours, bold),
 * ESC[2J (clear), ESC[H (home). printk output is tinted by log level.
 */
#include <hix/kernel.h>
#include <hix/console.h>
#include <hix/spinlock.h>
#include <steel/steel.h>

#define COLS		80
#define ROWS		25
#define HIST_LINES	200

#define DEFAULT_ATTR	STEEL_ATTR(STEEL_LGRAY, STEEL_BLACK)

static steel_cell_t screen_cells[COLS * ROWS];
static steel_cell_t view_cells[COLS * ROWS];
static steel_cell_t history[HIST_LINES][COLS];
static struct steel_surface screen, view;

static int cur_x, cur_y;
static u8 cur_attr = DEFAULT_ATTR;
static int hist_count;			/* valid lines in the ring */
static int hist_head;			/* next slot to write */
static int view_back;			/* lines scrolled back, 0 = live */

static DEFINE_SPINLOCK(con_lock);
static bool suspended;			/* a full-screen program owns the display */

/* ANSI escape parser state */
static enum { ST_NORMAL, ST_ESC, ST_CSI } state;
static int csi_args[4];
static int csi_nargs;

static const u8 ansi_to_vga[8] = {
	STEEL_BLACK, STEEL_RED, STEEL_GREEN, STEEL_BROWN,
	STEEL_BLUE, STEEL_MAGENTA, STEEL_CYAN, STEEL_LGRAY,
};

static void blank_line(steel_cell_t *line)
{
	int i;

	for (i = 0; i < COLS; i++)
		line[i] = STEEL_CELL(' ', cur_attr);
}

static void scroll_up(void)
{
	memcpy(history[hist_head], screen.cells, COLS * sizeof(steel_cell_t));
	hist_head = (hist_head + 1) % HIST_LINES;
	if (hist_count < HIST_LINES)
		hist_count++;

	memmove(screen.cells, screen.cells + COLS,
		(ROWS - 1) * COLS * sizeof(steel_cell_t));
	blank_line(screen.cells + (ROWS - 1) * COLS);
}

static void newline(void)
{
	cur_x = 0;
	if (++cur_y >= ROWS) {
		scroll_up();
		cur_y = ROWS - 1;
	}
}

static void put_char(char c)
{
	switch (c) {
	case '\n':
		newline();
		return;
	case '\r':
		cur_x = 0;
		return;
	case '\b':
		if (cur_x > 0) {
			cur_x--;
			steel_put(&screen, cur_x, cur_y, ' ', cur_attr);
		}
		return;
	case '\t':
		cur_x = (cur_x + 8) & ~7;
		if (cur_x >= COLS)
			newline();
		return;
	case '\f':
		steel_fill(&screen, ' ', cur_attr);
		cur_x = cur_y = 0;
		return;
	default:
		break;
	}
	steel_put(&screen, cur_x, cur_y, (u8)c, cur_attr);
	if (++cur_x >= COLS)
		newline();
}

static void apply_sgr(int n)
{
	if (n == 0) {
		cur_attr = DEFAULT_ATTR;
	} else if (n == 1) {
		cur_attr |= 0x08;		/* bright foreground */
	} else if (n >= 30 && n <= 37) {
		cur_attr = (cur_attr & 0xF8) | ansi_to_vga[n - 30];
	} else if (n >= 90 && n <= 97) {
		cur_attr = (cur_attr & 0xF0) | (ansi_to_vga[n - 90] | 0x08);
	} else if (n >= 40 && n <= 47) {
		cur_attr = (cur_attr & 0x0F) | (ansi_to_vga[n - 40] << 4);
	}
}

static void csi_final(char c)
{
	int i;

	switch (c) {
	case 'm':
		if (csi_nargs == 0)
			apply_sgr(0);
		for (i = 0; i < csi_nargs; i++)
			apply_sgr(csi_args[i]);
		break;
	case 'J':
		if (csi_nargs && csi_args[0] == 2) {
			steel_fill(&screen, ' ', cur_attr);
			cur_x = cur_y = 0;
		}
		break;
	case 'H':
		/* ESC[row;colH, 1-based; no arguments = home */
		cur_y = csi_nargs > 0 && csi_args[0] > 0 ? csi_args[0] - 1 : 0;
		cur_x = csi_nargs > 1 && csi_args[1] > 0 ? csi_args[1] - 1 : 0;
		if (cur_y >= ROWS)
			cur_y = ROWS - 1;
		if (cur_x >= COLS)
			cur_x = COLS - 1;
		break;
	case 'A':
	case 'B':
	case 'C':
	case 'D': {
		int n = csi_nargs && csi_args[0] > 0 ? csi_args[0] : 1;

		if (c == 'A')
			cur_y = cur_y >= n ? cur_y - n : 0;
		else if (c == 'B')
			cur_y = cur_y + n < ROWS ? cur_y + n : ROWS - 1;
		else if (c == 'C')
			cur_x = cur_x + n < COLS ? cur_x + n : COLS - 1;
		else
			cur_x = cur_x >= n ? cur_x - n : 0;
		break;
	}
	case 'K': {
		/* erase in line: 0 = cursor to end, 1 = start to cursor, 2 = all */
		int mode = csi_nargs ? csi_args[0] : 0;
		int from = mode == 0 ? cur_x : 0;
		int to = mode == 1 ? cur_x + 1 : COLS;

		for (i = from; i < to && i < COLS; i++)
			steel_put(&screen, i, cur_y, ' ', cur_attr);
		break;
	}
	default:
		break;
	}
}

static void feed(char c)
{
	switch (state) {
	case ST_NORMAL:
		if (c == 0x1B)
			state = ST_ESC;
		else
			put_char(c);
		break;
	case ST_ESC:
		if (c == '[') {
			state = ST_CSI;
			csi_nargs = 0;
			memset(csi_args, 0, sizeof(csi_args));
		} else {
			state = ST_NORMAL;
		}
		break;
	case ST_CSI:
		if (c >= '0' && c <= '9') {
			if (csi_nargs == 0)
				csi_nargs = 1;
			csi_args[csi_nargs - 1] =
				csi_args[csi_nargs - 1] * 10 + (c - '0');
		} else if (c == ';') {
			if (csi_nargs == 0)
				csi_nargs = 1;
			if (csi_nargs < 4)
				csi_nargs++;
		} else {
			csi_final(c);
			state = ST_NORMAL;
		}
		break;
	}
}

/* build what the user currently sees: scrollback window or live screen */
static const struct steel_surface *current_view(void)
{
	int total = hist_count + ROWS;
	int first = total - ROWS - view_back;	/* index of the top row */
	int row;

	if (view_back == 0)
		return &screen;

	for (row = 0; row < ROWS; row++) {
		int idx = first + row;
		const steel_cell_t *src;

		if (idx < hist_count) {
			int slot = (hist_head - hist_count + idx + HIST_LINES *
				    2) % HIST_LINES;

			src = history[slot];
		} else {
			src = screen.cells + (idx - hist_count) * COLS;
		}
		memcpy(view.cells + row * COLS, src,
		       COLS * sizeof(steel_cell_t));
	}
	return &view;
}

static void refresh(void)
{
	if (suspended)
		return;
	steel_present(current_view());
	steel_set_cursor(cur_x, cur_y, view_back == 0);
}

void steel_con_init(void)
{
	steel_surface_init(&screen, COLS, ROWS, screen_cells);
	steel_surface_init(&view, COLS, ROWS, view_cells);
	cur_attr = DEFAULT_ATTR;
	steel_fill(&screen, ' ', cur_attr);
	cur_x = cur_y = 0;
	hist_count = hist_head = view_back = 0;
	state = ST_NORMAL;
}

void steel_con_write(const char *s, size_t n)
{
	unsigned long flags;

	spin_lock_irqsave(&con_lock, flags);
	view_back = 0;			/* new output snaps back to live */
	while (n--)
		feed(*s++);
	refresh();
	spin_unlock_irqrestore(&con_lock, flags);
}

void steel_con_set_attr(u8 attr)
{
	cur_attr = attr;
}

void steel_con_clear(void)
{
	steel_con_write("\x1b[2J", 4);
}

void steel_con_scroll_view(int delta)
{
	unsigned long flags;

	spin_lock_irqsave(&con_lock, flags);
	if (delta == 0) {
		view_back = 0;
	} else {
		view_back += delta;
		if (view_back < 0)
			view_back = 0;
		if (view_back > hist_count)
			view_back = hist_count;
	}
	refresh();
	spin_unlock_irqrestore(&con_lock, flags);
}

/* while suspended the console keeps its state but does not draw */
void steel_con_set_suspended(bool on)
{
	unsigned long flags;

	spin_lock_irqsave(&con_lock, flags);
	suspended = on;
	if (on)
		steel_set_cursor(0, 0, false);
	else
		refresh();
	spin_unlock_irqrestore(&con_lock, flags);
}

void steel_con_redraw(void)
{
	unsigned long flags;

	spin_lock_irqsave(&con_lock, flags);
	refresh();
	spin_unlock_irqrestore(&con_lock, flags);
}

struct steel_surface *steel_con_surface(void)
{
	return &screen;
}

/* ---- printk sink ------------------------------------------------------ */

static u8 level_attr(int level)
{
	if (level <= 3)
		return STEEL_ATTR(STEEL_LRED, STEEL_BLACK);
	if (level == 4)
		return STEEL_ATTR(STEEL_YELLOW, STEEL_BLACK);
	if (level == 7)
		return STEEL_ATTR(STEEL_DGRAY, STEEL_BLACK);
	return DEFAULT_ATTR;
}

static void printk_sink(struct console *con, int level, const char *s,
			size_t n)
{
	u8 saved = cur_attr;

	cur_attr = level_attr(level);
	steel_con_write(s, n);
	cur_attr = saved;
}

static struct console steel_console = {
	.name = "steel",
	.max_level = 6,			/* info and above; not debug */
	.write = printk_sink,
};

void steel_con_register_printk(void)
{
	register_console(&steel_console);
}
