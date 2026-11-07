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
		mem[i] = STEEL_TEXT(s->cells[i]);
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

/*
 * Giving the screen back after a pixel mode.
 *
 * Switching an adapter to a linear framebuffer rewrites the VGA registers,
 * and writing the framebuffer overwrites the memory that holds the text font.
 * Switching back is therefore not enough: the text mode has to be programmed
 * again and the font loaded again, or the screen stays black. The font is
 * saved while the adapter is still in text mode (vga_font_save()).
 */
#define FONT_GLYPHS	256
#define FONT_STRIDE	32
#define FONT_MEM	((volatile u8 *)0xA0000)

static u8 saved_font[FONT_GLYPHS * FONT_STRIDE];
static bool font_saved;

static void seq_w(u8 idx, u8 val)
{
	outb(0x3C4, idx);
	outb(0x3C5, val);
}

static void gc_w(u8 idx, u8 val)
{
	outb(0x3CE, idx);
	outb(0x3CF, val);
}

/* map font plane 2 at 0xA0000 for reading or writing */
static void font_plane_open(void)
{
	seq_w(0x00, 0x01);		/* synchronous reset */
	seq_w(0x02, 0x04);		/* write plane 2 only */
	seq_w(0x04, 0x07);		/* sequential, no chain-4 */
	seq_w(0x00, 0x03);
	gc_w(0x04, 0x02);		/* read plane 2 */
	gc_w(0x05, 0x00);
	gc_w(0x06, 0x00);		/* 0xA0000, 128 KiB */
}

void vga_font_save(void)
{
	int i;

	if (font_saved)
		return;
	font_plane_open();
	for (i = 0; i < FONT_GLYPHS * FONT_STRIDE; i++)
		saved_font[i] = FONT_MEM[i];
	font_saved = true;
}

static const u8 mode3_crtc[25] = {
	0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F, 0x00, 0x4F, 0x0D, 0x0E,
	0x00, 0x00, 0x00, 0x00, 0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
	0xFF,
};
static const u8 mode3_seq[5] = { 0x03, 0x00, 0x03, 0x00, 0x02 };
static const u8 mode3_gc[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF };
static const u8 mode3_ac[21] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38, 0x39, 0x3A, 0x3B,
	0x3C, 0x3D, 0x3E, 0x3F, 0x04, 0x00, 0x0F, 0x08, 0x00,
};

void vga_text_restore(void)
{
	int i;

	if (font_saved) {
		font_plane_open();
		for (i = 0; i < FONT_GLYPHS * FONT_STRIDE; i++)
			FONT_MEM[i] = saved_font[i];
	}

	outb(0x3C2, 0x67);			/* 80x25, colour ports */
	seq_w(0x00, 0x01);
	for (i = 1; i < 5; i++)
		seq_w((u8)i, mode3_seq[i]);
	seq_w(0x00, 0x03);
	outb(0x3D4, 0x11);			/* unlock CRTC registers 0-7 */
	outb(0x3D5, inb(0x3D5) & 0x7F);
	for (i = 0; i < 25; i++) {
		outb(0x3D4, (u8)i);
		outb(0x3D5, mode3_crtc[i]);
	}
	for (i = 0; i < 9; i++)
		gc_w((u8)i, mode3_gc[i]);
	inb(0x3DA);
	for (i = 0; i < 21; i++) {
		outb(0x3C0, (u8)i);
		outb(0x3C0, mode3_ac[i]);
	}
	inb(0x3DA);
	outb(0x3C0, 0x20);			/* screen on */

	last_pos = last_visible = -1;		/* the cursor registers are new */
	for (i = 0; i < COLS * ROWS; i++)
		VGA_MEM[i] = 0x0720;
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
