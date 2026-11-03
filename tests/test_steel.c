// SPDX-License-Identifier: GPL-2.0-only
#include "hosttest.h"
#include <hix/kernel.h>
#include <aether/kalloc.h>
#include <steel/steel.h>

static int presents;
static struct steel_surface last;
static steel_cell_t last_cells[80 * 25];
static int cur_x, cur_y;

static void fake_present(struct steel_device *dev,
			 const struct steel_surface *s)
{
	(void)dev;
	presents++;
	memcpy(last_cells, s->cells, sizeof(last_cells));
	last.w = s->w;
	last.h = s->h;
	last.cells = last_cells;
}

static void fake_cursor(struct steel_device *dev, int x, int y, bool vis)
{
	(void)dev;
	(void)vis;
	cur_x = x;
	cur_y = y;
}

static struct steel_device fake_dev = {
	.name = "fake", .w = 80, .h = 25,
	.present = fake_present, .set_cursor = fake_cursor,
};

static char row_text[81];

static const char *row(const struct steel_surface *s, int y)
{
	int x;

	for (x = 0; x < s->w && x < 80; x++)
		row_text[x] = (char)STEEL_CH(steel_get(s, x, y));
	while (x > 0 && row_text[x - 1] == ' ')
		x--;
	row_text[x] = 0;
	return row_text;
}

static void con_puts(const char *s)
{
	steel_con_write(s, strlen(s));
}

int main(void)
{
	struct steel_surface *s;
	struct steel_cmdbuf *cb;
	struct steel_layer *l;
	int i;

	kalloc_init_region(malloc(1 << 20), 1 << 20);
	steel_set_device(&fake_dev);

	/* primitives and clipping */
	s = steel_surface_create(10, 4);
	CHECK(s != NULL);
	steel_fill(s, '.', 0x07);
	steel_put(s, 1, 1, 'A', 0x1F);
	steel_put(s, -1, 0, 'X', 0x1F);		/* clipped */
	steel_put(s, 10, 0, 'X', 0x1F);
	CHECK(steel_get(s, 1, 1) == STEEL_CELL('A', 0x1F));
	CHECK(steel_get(s, 99, 99) == 0);
	CHECK(steel_puts(s, 8, 0, "hello", 0x07) == 2);	/* only "he" fits */
	CHECK_STR(row(s, 0), "........he");

	steel_box(s, 0, 0, 4, 3, STEEL_BOX_SINGLE, 0x07);
	CHECK(STEEL_CH(steel_get(s, 0, 0)) == 0xDA);
	CHECK(STEEL_CH(steel_get(s, 3, 2)) == 0xD9);
	CHECK(STEEL_CH(steel_get(s, 1, 0)) == 0xC4);
	CHECK(STEEL_CH(steel_get(s, 0, 1)) == 0xB3);

	steel_tint(s, 0, 0, 2, 1, 0x4E);
	CHECK(STEEL_AT(steel_get(s, 0, 0)) == 0x4E &&
	      STEEL_CH(steel_get(s, 0, 0)) == 0xDA);

	{
		struct steel_surface *d = steel_surface_create(6, 2);

		steel_fill(d, ' ', 0x07);
		steel_blit(d, 4, 1, s, 0, 0, 5, 5);	/* clipped on both */
		CHECK(STEEL_CH(steel_get(d, 4, 1)) == 0xDA);
		CHECK(STEEL_CH(steel_get(d, 3, 1)) == ' ');
		steel_surface_destroy(d);
	}
	steel_surface_destroy(s);

	/* command buffer: nothing happens before commit */
	s = steel_surface_create(80, 25);
	cb = kzalloc(sizeof(*cb));
	steel_cmdbuf_begin(cb, s);
	steel_enc_clear(cb, ' ', 0x07);
	steel_enc_text(cb, 2, 3, "steel", 0x0F);
	steel_enc_fill_rect(cb, 0, 0, 3, 1, '#', 0x70);
	CHECK(cb->count == 3 && STEEL_CH(steel_get(s, 2, 3)) == 0);
	presents = 0;
	CHECK(steel_commit(cb) == 3);
	CHECK(presents == 1);
	CHECK_STR(row(&last, 3) + 2, "steel");
	CHECK(STEEL_CH(steel_get(&last, 0, 0)) == '#');
	for (i = 0; i < STEEL_CMDBUF_MAX + 5; i++)
		if (steel_enc_hline(cb, 0, 0, 1, '-', 7) == -ENOSPC)
			break;
	CHECK(i == STEEL_CMDBUF_MAX);			/* overflow is reported */
	kfree(cb);

	/* compositor: window frame, title, content, shadow, z-order */
	l = steel_layer_create(5, 5, 10, 3, "win");
	steel_puts(l->content, 0, 0, "content", 0x07);
	steel_compose(s, "menu");
	CHECK_STR(row(s, 0) + 1, "menu");
	CHECK(STEEL_CH(steel_get(s, 5, 5)) == 0xC9);		/* double frame */
	CHECK(STEEL_CH(steel_get(s, 6, 6)) == 'c');
	CHECK(STEEL_AT(steel_get(s, 5 + 12, 6)) == 0x08);	/* shadow */
	{
		int found = 0, x;

		for (x = 5; x < 18; x++)
			if (STEEL_CH(steel_get(s, x, 5)) == 'w')
				found = 1;
		CHECK(found);					/* title drawn */
	}
	{
		struct steel_layer *top = steel_layer_create(6, 6, 4, 2, "t");

		steel_compose(s, NULL);
		CHECK(STEEL_CH(steel_get(s, 6, 6)) == 0xC9);	/* covers 'c' */
		steel_layer_raise(l);
		steel_compose(s, NULL);
		CHECK(STEEL_CH(steel_get(s, 6, 6)) == 'c');	/* raised */
		steel_layer_destroy(top);
	}
	steel_layer_destroy(l);
	steel_surface_destroy(s);

	/* console: text, wrapping, colours, scrollback */
	steel_con_init();
	con_puts("hi\n");
	CHECK_STR(row(&last, 0), "hi");
	CHECK(cur_x == 0 && cur_y == 1);
	con_puts("ab\bc");
	CHECK(STEEL_CH(steel_get(&last, 0, 1)) == 'a' &&
	      STEEL_CH(steel_get(&last, 1, 1)) == 'c');

	con_puts("\x1b[31mred\x1b[0m");
	CHECK(STEEL_AT(steel_get(&last, 2, 1)) == STEEL_ATTR(STEEL_RED, 0));
	con_puts("x");
	CHECK(STEEL_AT(steel_get(&last, 5, 1)) == STEEL_ATTR(STEEL_LGRAY, 0));
	con_puts("\x1b[1;32mg");
	CHECK(STEEL_AT(steel_get(&last, 6, 1)) ==
	      STEEL_ATTR(STEEL_LGREEN, STEEL_BLACK));
	con_puts("\x1b[0m");

	con_puts("\x1b[2J");
	CHECK(STEEL_CH(steel_get(&last, 0, 0)) == ' ' && cur_x == 0 && cur_y == 0);

	for (i = 0; i < 40; i++) {			/* scroll off 15+ lines */
		char line[16];

		snprintf(line, sizeof(line), "line%d\n", i);
		con_puts(line);
	}
	CHECK_STR(row(&last, 23), "line39");		/* live bottom */
	steel_con_scroll_view(10);
	CHECK_STR(row(&last, 23), "line29");		/* 10 lines back */
	steel_con_scroll_view(-3);
	CHECK_STR(row(&last, 23), "line32");
	steel_con_scroll_view(1000);			/* clamped to history */
	steel_con_scroll_view(0);
	CHECK_STR(row(&last, 23), "line39");
	steel_con_scroll_view(5);
	con_puts("z");					/* output snaps to live */
	CHECK_STR(row(&last, 24), "z");

	/* long lines wrap */
	steel_con_init();
	for (i = 0; i < 85; i++)
		con_puts("w");
	CHECK(STEEL_CH(steel_get(&last, 0, 1)) == 'w' && cur_y == 1 && cur_x == 5);

	TEST_DONE();
}
