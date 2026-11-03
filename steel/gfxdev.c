// SPDX-License-Identifier: GPL-2.0-only
/* steel gfxdev: the pixel display (see gfxdev.h). */
#include <hix/kernel.h>
#include <steel/bga.h>
#include <steel/gfxdev.h>

static struct pixdev pixdev;
static struct steel_device *text_device;
static bool active;

static void gfx_present(struct steel_device *dev, const struct steel_surface *s)
{
	(void)dev;
	if (s->w == PIX_COLS && s->h == PIX_ROWS)
		pixdev_present(&pixdev, s->cells);
}

static void gfx_set_cursor(struct steel_device *dev, int x, int y, bool visible)
{
	(void)dev;
	(void)x;
	(void)y;
	(void)visible;			/* the text cursor does not exist here */
}

static struct steel_device gfx_device = {
	.name = "steel-pixel",
	.w = PIX_COLS,
	.h = PIX_ROWS,
	.present = gfx_present,
	.set_cursor = gfx_set_cursor,
};

int steel_gfx_enter(void)
{
	struct gfx_canvas fb;
	u32 *mem;

	if (active)
		return 0;
	mem = bga_enable(GFX_SCREEN_W, GFX_SCREEN_H);
	if (!mem)
		return -ENODEV;
	gfx_canvas_init(&fb, GFX_SCREEN_W, GFX_SCREEN_H, mem);
	pixdev_init(&pixdev, &fb);
	pixdev_cursor(&pixdev, GFX_SCREEN_W / 2, GFX_SCREEN_H / 2, true);
	text_device = steel_get_device();
	steel_set_device(&gfx_device);
	active = true;
	return 0;
}

void steel_gfx_leave(void)
{
	if (!active)
		return;
	active = false;
	steel_set_device(text_device);
	bga_disable();
}

bool steel_gfx_active(void)
{
	return active;
}

struct pixdev *steel_gfx_pixdev(void)
{
	return active ? &pixdev : NULL;
}
