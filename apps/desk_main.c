// SPDX-License-Identifier: GPL-2.0-only
/*
 * desk: the qubix program that runs the desktop.
 *
 * `desk` takes the display, then turns keyboard and mouse events into
 * window events until the user leaves Hub. An optional argument
 * names a program to start on top of Hub: `desk snake`.
 * Everything goes through libq, like any other qubix program.
 */
#include <hix/kernel.h>
#include <qubix/libq.h>
#include <desk/desk.h>

static int qb_wait_event(struct input_event *ev, long timeout_ms)
{
	return (int)q_steelctl(STEEL_CTL_EVENT, (long)ev, timeout_ms, 0);
}

static void qb_present(const steel_cell_t *cells, int w, int h)
{
	q_steelctl(STEEL_CTL_PRESENT, (long)cells, w, h);
}

static unsigned long qb_now_ms(void)
{
	return (unsigned long)q_steelctl(STEEL_CTL_MSEC, 0, 0, 0);
}

static int qb_client_set(int idx, int x, int y, int w, int h, const u32 *px,
			 int stride)
{
	struct steel_client_req rq = {
		.idx = idx, .x = x, .y = y, .w = w, .h = h,
		.px = (unsigned long)px, .stride = stride,
	};

	return (int)q_steelctl(STEEL_CTL_CLIENT_SET, (long)&rq, 0, 0);
}

static void qb_client_dirty(int idx, int x, int y, int w, int h)
{
	struct steel_client_req rq = { .idx = idx, .x = x, .y = y, .w = w, .h = h };

	q_steelctl(STEEL_CTL_CLIENT_DIRTY, (long)&rq, 0, 0);
}

static void qb_client_clear(int idx)
{
	q_steelctl(STEEL_CTL_CLIENT_CLEAR, idx, 0, 0);
}

const struct desk_backend desk_qubix_backend = {
	.wait_event = qb_wait_event,
	.present = qb_present,
	.now_ms = qb_now_ms,
	.client_set = qb_client_set,
	.client_dirty = qb_client_dirty,
	.client_clear = qb_client_clear,
};

int desk_main(int argc, char **argv)
{
	int info[2] = { 0, 0 };
	int gfx[6];
	long ret = q_steelctl(STEEL_CTL_GRAB, STEEL_GRAB_PIXELS, 0, 0);

	if (ret < 0) {
		q_printf("desk: cannot take the display (%ld)\n", ret);
		return 1;
	}
	q_steelctl(STEEL_CTL_INFO, (long)info, 0, 0);
	if (info[0] != DESK_W || info[1] != DESK_H) {
		q_steelctl(STEEL_CTL_UNGRAB, 0, 0, 0);
		q_printf("desk: needs an %dx%d display, have %dx%d\n", DESK_W,
			 DESK_H, info[0], info[1]);
		return 1;
	}
	if (ret == 1 && !q_steelctl(STEEL_CTL_GFXINFO, (long)gfx, 0, 0))
		desk_set_pixels(true, gfx[4], gfx[5]);
	desk_seed((u32)q_steelctl(STEEL_CTL_MSEC, 0, 0, 0) * 2654435761u);
	desk_run(&desk_qubix_backend, argc > 1 ? argv[1] : NULL);
	q_steelctl(STEEL_CTL_UNGRAB, 0, 0, 0);
	return 0;
}
