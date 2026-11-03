// SPDX-License-Identifier: GPL-2.0-only
/* VGA text mode (80x25) as a steel display device. */
#define pr_fmt(fmt) "vga: " fmt

#include <hix/kernel.h>
#include <hix/driver.h>
#include <asm/io.h>
#include <steel/steel.h>

#define VGA_MEM		((volatile u16 *)0xB8000)
#define CRTC_INDEX	0x3D4
#define CRTC_DATA	0x3D5
#define COLS		80
#define ROWS		25

static int last_pos = -1;
static int last_visible = -1;

static void vga_present(struct steel_device *dev,
			const struct steel_surface *s)
{
	volatile u16 *mem = VGA_MEM;
	int i, n = COLS * ROWS;

	(void)dev;
	if (s->w != COLS || s->h != ROWS)
		return;
	for (i = 0; i < n; i++)
		mem[i] = s->cells[i];
}

static void vga_set_cursor(struct steel_device *dev, int x, int y,
			   bool visible)
{
	int pos = y * COLS + x;

	(void)dev;
	if (visible != last_visible) {
		outb(CRTC_INDEX, 0x0A);
		outb(CRTC_DATA, visible ? 0x0D : 0x20);	/* start line / off */
		outb(CRTC_INDEX, 0x0B);
		outb(CRTC_DATA, 0x0F);			/* end line */
		last_visible = visible;
	}
	if (pos != last_pos) {
		outb(CRTC_INDEX, 0x0F);
		outb(CRTC_DATA, pos & 0xFF);
		outb(CRTC_INDEX, 0x0E);
		outb(CRTC_DATA, (pos >> 8) & 0xFF);
		last_pos = pos;
	}
}

static struct steel_device vga_device = {
	.name = "vga-text",
	.w = COLS,
	.h = ROWS,
	.present = vga_present,
	.set_cursor = vga_set_cursor,
};

/*
 * Bit 3 of the attribute mode control register makes the top attribute bit
 * "blink" instead of "bright background". Clearing it gives 16 background
 * colours, which the desktop needs for white windows.
 */
static void vga_disable_blink(void)
{
	u8 mode;

	inb(0x3DA);			/* reset the attribute flip-flop */
	outb(0x3C0, 0x30);		/* index 0x10, keep the screen enabled */
	mode = inb(0x3C1);
	outb(0x3C0, mode & (u8)~0x08);
}

static int vga_start(void)
{
	vga_disable_blink();
	steel_con_init();
	steel_set_device(&vga_device);
	steel_con_register_printk();
	return 0;
}

static struct hix_driver vga_driver = {
	.name = "vga",
	.class = "display",
	.level = DRV_LEVEL_CONSOLE,
	.start = vga_start,
};
HIX_DRIVER(vga_driver);
