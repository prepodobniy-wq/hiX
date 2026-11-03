// SPDX-License-Identifier: GPL-2.0-only
/*
 * steel: the demo behind the `gfx` shell command. Records a desktop with a
 * command buffer, stacks three live windows with the compositor, animates
 * one of them, and waits for a key.
 */
#include <hix/kernel.h>
#include <aether/kalloc.h>
#include <aether/pmm.h>
#include <aether/sched.h>
#include <aether/task.h>
#include <qubix/tty.h>
#include <steel/steel.h>

#define ROWS	25
#define COLS	80

static void fill_stats(struct steel_layer *l, int frame)
{
	struct steel_surface *c = l->content;
	char line[40];
	u8 attr = STEEL_ATTR(STEEL_BLACK, STEEL_LGRAY);
	int bar, i;

	steel_fill(c, ' ', attr);
	snprintf(line, sizeof(line), "uptime  %lu s", jiffies / HZ);
	steel_puts(c, 1, 0, line, attr);
	snprintf(line, sizeof(line), "free    %lu KiB",
		 pmm_free_count() * PAGE_SIZE / 1024);
	steel_puts(c, 1, 1, line, attr);
	snprintf(line, sizeof(line), "frame   %d", frame);
	steel_puts(c, 1, 2, line, attr);

	bar = (frame * (c->w - 4)) / 60;
	steel_puts(c, 1, 4, "[", attr);
	for (i = 0; i < c->w - 4; i++)
		steel_put(c, 2 + i, 4, i < bar ? STEEL_BLOCK_FULL : 0xB0,
			  STEEL_ATTR(STEEL_GREEN, STEEL_LGRAY));
	steel_puts(c, c->w - 2, 4, "]", attr);
}

void steel_demo(void)
{
	static const char *names[] = { "aether", "qubix", "steel", "hix" };
	struct steel_surface *frame_buf;
	struct steel_layer *stats, *about, *mover;
	struct steel_cmdbuf *cb;
	int frame, i;

	frame_buf = steel_surface_create(COLS, ROWS);
	cb = kzalloc(sizeof(*cb));
	stats = steel_layer_create(4, 3, 30, 6, "system");
	about = steel_layer_create(30, 8, 40, 9, "steel");
	mover = steel_layer_create(10, 14, 24, 5, "aether");
	if (!frame_buf || !cb || !stats || !about || !mover)
		goto out;

	steel_puts(about->content, 2, 1, "hix is a hybrid kernel:",
		   STEEL_ATTR(STEEL_BLACK, STEEL_LGRAY));
	for (i = 0; i < 4; i++)
		steel_puts(about->content, 4, 3 + i, names[i],
			   STEEL_ATTR(i == 2 ? STEEL_RED : STEEL_BLUE,
				      STEEL_LGRAY));
	steel_puts(about->content, 14, 3, "task and port core", STEEL_ATTR(0, 7));
	steel_puts(about->content, 14, 4, "process personality", STEEL_ATTR(0, 7));
	steel_puts(about->content, 14, 5, "this graphics layer", STEEL_ATTR(0, 7));
	steel_puts(about->content, 14, 6, "the whole system", STEEL_ATTR(0, 7));
	steel_puts(mover->content, 2, 2, "I am a layer.",
		   STEEL_ATTR(STEEL_MAGENTA, STEEL_LGRAY));

	for (frame = 0; frame <= 60; frame++) {
		struct steel_cmdbuf *c = cb;

		fill_stats(stats, frame);
		steel_layer_move(mover, 10 + frame / 2, 14 - (frame % 20) / 5);
		steel_compose(frame_buf, " hix  steel demo - any key to leave");

		/* the footer goes through a command buffer */
		steel_cmdbuf_begin(c, frame_buf);
		steel_enc_hline(c, 0, ROWS - 1, COLS, ' ',
				STEEL_ATTR(STEEL_BLACK, STEEL_LGRAY));
		steel_enc_text(c, 2, ROWS - 1,
			       "command buffer: 3 commands, 1 present",
			       STEEL_ATTR(STEEL_BLACK, STEEL_LGRAY));
		steel_enc_tint(c, COLS - 8, ROWS - 1, 8, 1,
			       STEEL_ATTR(STEEL_WHITE, STEEL_RED));
		steel_commit(c);
		steel_set_cursor(0, 0, false);
		thread_sleep(msecs_to_jiffies(60));
	}
	tty_getc();
out:
	if (stats)
		steel_layer_destroy(stats);
	if (about)
		steel_layer_destroy(about);
	if (mover)
		steel_layer_destroy(mover);
	steel_surface_destroy(frame_buf);
	kfree(cb);
	steel_con_redraw();
}
