// SPDX-License-Identifier: GPL-2.0-only
/*
 * qubix: input routing and the event queue.
 *
 * Interrupt handlers push into a ring; the owner of the display sleeps on a
 * wait queue until something arrives. A mouse that moves faster than the
 * reader is coalesced into one pending event, so a flood of motion packets
 * can never push a key press out of the queue.
 */
#include <hix/kernel.h>
#include <hix/spinlock.h>
#include <aether/waitq.h>
#include <steel/steel.h>
#include <qubix/input.h>
#include <qubix/tty.h>

#define EV_RING		64		/* power of two */
/*
 * The pointer lives in a "space" of space_w x space_h units. In text mode a
 * unit is 1/8 of a column by 1/16 of a row (a 640x400 space); on the pixel
 * display it is a pixel. Events carry both the cell under the pointer and
 * the position inside the space.
 */

static struct input_event ring[EV_RING];
static unsigned int head, tail;
static bool grabbed;
static DECLARE_WAITQ(event_wq);

static int mouse_x, mouse_y;		/* in mickeys */
static u8 mouse_buttons;
static int last_cx = -1, last_cy = -1;
static struct {
	int space_w, space_h;
	int cell_w, cell_h;
	int off_x, off_y;
	int cols, rows;
	bool pixels;
} geo = { 640, 400, 8, 16, 0, 0, 80, 25, false };


static void push(const struct input_event *ev)
{
	unsigned int next = (head + 1) & (EV_RING - 1);

	if (next == tail)
		return;			/* full: drop the newest */
	ring[head] = *ev;
	head = next;
	waitq_wake_one(&event_wq);
}

void input_key(int key, u8 mods)
{
	struct input_event ev;

	if (grabbed) {
		memset(&ev, 0, sizeof(ev));
		ev.type = INPUT_KEY;
		ev.mods = mods;
		ev.key = (u16)key;
		push(&ev);
		return;
	}

	/* the console: PgUp/PgDn scroll, ASCII feeds the line discipline */
	if (key == KEY_PGUP) {
		steel_con_scroll_view(12);
	} else if (key == KEY_PGDN) {
		steel_con_scroll_view(-12);
	} else if (key < 0x100 || tty_is_raw()) {
		if ((mods & MOD_CTRL) && ((key | 0x20) >= 'a') &&
		    ((key | 0x20) <= 'z'))
			key &= 0x1F;
		tty_input(key);
	}
}

void input_mouse(int dx, int dy, u8 buttons)
{
	struct input_event ev;
	int cx, cy;

	if (!grabbed)
		return;

	mouse_x += dx;
	mouse_y += dy;
	if (mouse_x < 0)
		mouse_x = 0;
	if (mouse_x >= geo.space_w)
		mouse_x = geo.space_w - 1;
	if (mouse_y < 0)
		mouse_y = 0;
	if (mouse_y >= geo.space_h)
		mouse_y = geo.space_h - 1;
	cx = (mouse_x - geo.off_x) / geo.cell_w;
	cy = (mouse_y - geo.off_y) / geo.cell_h;
	if (mouse_x < geo.off_x)
		cx = 0;
	if (mouse_y < geo.off_y)
		cy = 0;
	cx = cx < 0 ? 0 : cx >= geo.cols ? geo.cols - 1 : cx;
	cy = cy < 0 ? 0 : cy >= geo.rows ? geo.rows - 1 : cy;
	/* in text mode only a change of cell matters; pixels always do */
	if (!geo.pixels && cx == last_cx && cy == last_cy &&
	    buttons == mouse_buttons)
		return;
	last_cx = cx;
	last_cy = cy;

	memset(&ev, 0, sizeof(ev));
	ev.type = INPUT_MOUSE;
	ev.x = (s16)cx;
	ev.y = (s16)cy;
	ev.px = (s16)mouse_x;
	ev.py = (s16)mouse_y;
	ev.buttons = buttons;

	if (head != tail) {
		struct input_event *last = &ring[(head - 1) & (EV_RING - 1)];

		if (last->type == INPUT_MOUSE && last->buttons == buttons &&
		    mouse_buttons == buttons) {
			last->x = ev.x;		/* merge into the pending move */
			last->y = ev.y;
			last->px = ev.px;
			last->py = ev.py;
			return;
		}
	}
	mouse_buttons = buttons;
	push(&ev);
}

/* the display changed: tell the pointer how big its world is */
void input_set_geometry(int space_w, int space_h, int cell_w, int cell_h,
			int off_x, int off_y, bool pixels)
{
	unsigned long flags;

	local_irq_save(flags);
	geo.space_w = space_w;
	geo.space_h = space_h;
	geo.cell_w = cell_w;
	geo.cell_h = cell_h;
	geo.off_x = off_x;
	geo.off_y = off_y;
	geo.pixels = pixels;
	mouse_x = space_w / 2;
	mouse_y = space_h / 2;
	last_cx = last_cy = -1;
	local_irq_restore(flags);
}

void input_get_pointer(int *x, int *y)
{
	*x = mouse_x;
	*y = mouse_y;
}

int input_grab(void)
{
	unsigned long flags;
	struct steel_device *dev = steel_get_device();

	local_irq_save(flags);
	if (grabbed) {
		local_irq_restore(flags);
		return -EBUSY;
	}
	if (dev) {
		geo.cols = dev->w;
		geo.rows = dev->h;
	}
	grabbed = true;
	head = tail = 0;
	mouse_x = geo.space_w / 2;
	mouse_y = geo.space_h / 2;
	mouse_buttons = 0;
	last_cx = last_cy = -1;
	local_irq_restore(flags);
	return 0;
}

void input_release(void)
{
	unsigned long flags;

	local_irq_save(flags);
	grabbed = false;
	head = tail = 0;
	local_irq_restore(flags);
}

bool input_grabbed(void)
{
	return grabbed;
}

int input_wait(struct input_event *ev, long timeout_ms)
{
	unsigned long flags, deadline = 0;
	int ret = 0;

	if (timeout_ms > 0)
		deadline = jiffies + msecs_to_jiffies((unsigned long)timeout_ms);

	local_irq_save(flags);
	while (head == tail) {
		long left = 0;

		if (timeout_ms == 0) {
			ret = -ETIMEDOUT;
			break;
		}
		if (timeout_ms > 0) {
			left = (long)(deadline - jiffies);
			if (left <= 0) {
				ret = -ETIMEDOUT;
				break;
			}
		}
		waitq_wait_irq(&event_wq, (unsigned long)left);
	}
	if (!ret) {
		*ev = ring[tail];
		tail = (tail + 1) & (EV_RING - 1);
	}
	local_irq_restore(flags);
	return ret;
}
