// SPDX-License-Identifier: GPL-2.0-only
/* Tests for steel gfx: canvas drawing and the cell-to-pixel device. */
#include "hosttest.h"
#include <string.h>
#include <hix/kernel.h>
#include <steel/pixdev.h>

#define W 1024
#define H 768

static u32 fbmem[W * H];
static struct gfx_canvas fb;

static u32 at(int x, int y)
{
	return fb.px[y * fb.stride + x];
}

static void test_basics(void)
{
	printf("-- canvas primitives\n");
	gfx_canvas_init(&fb, W, H, fbmem);
	gfx_fill(&fb, 0, 0, W, H, 0x000000);

	gfx_fill(&fb, 10, 10, 20, 20, 0xFF0000);
	CHECK(at(10, 10) == 0xFF0000 && at(29, 29) == 0xFF0000);
	CHECK(at(9, 10) == 0 && at(30, 10) == 0 && at(10, 30) == 0);

	/* clipping: nothing outside the clip rectangle changes */
	gfx_clip_set(&fb, 100, 100, 10, 10);
	gfx_fill(&fb, 90, 90, 40, 40, 0x00FF00);
	CHECK(at(99, 100) == 0 && at(100, 100) == 0x00FF00);
	CHECK(at(109, 109) == 0x00FF00 && at(110, 109) == 0);
	gfx_clip_reset(&fb);

	/* drawing off the canvas must not crash or wrap */
	gfx_fill(&fb, -50, -50, 60, 60, 0x0000FF);
	gfx_fill(&fb, W - 5, H - 5, 50, 50, 0x0000FF);
	CHECK(at(0, 0) == 0x0000FF && at(W - 1, H - 1) == 0x0000FF);
	CHECK(at(W - 1, 0) == 0);

	CHECK(gfx_mix(0x000000, 0xFFFFFF, 0) == 0x000000);
	CHECK(gfx_mix(0x000000, 0xFFFFFF, 255) == 0xFFFFFF);
	CHECK(GFX_R(gfx_mix(0, 0xFF0000, 128)) == 128);

	CHECK(gfx_isqrt(0) == 0 && gfx_isqrt(1) == 1 && gfx_isqrt(15) == 3);
	CHECK(gfx_isqrt(16) == 4 && gfx_isqrt(1000000) == 1000);
	CHECK(gfx_isqrt(0xFFFFFFFFu) == 65535);
}

static void test_shapes(void)
{
	u32 corner, edge;

	printf("-- rounded rectangles, discs, rings\n");
	gfx_fill(&fb, 0, 0, W, H, 0x000000);
	gfx_rrect(&fb, 200, 200, 100, 60, 12, 0xFFFFFF, 255);
	CHECK(at(250, 230) == 0xFFFFFF);		/* middle */
	CHECK(at(200, 230) == 0xFFFFFF);		/* straight left edge */
	CHECK(at(200, 200) == 0x000000);		/* the corner pixel is cut */
	corner = at(203, 203);
	CHECK(corner != 0 && corner != 0xFFFFFF);	/* anti-aliased */
	edge = at(212, 200);
	CHECK(edge == 0xFFFFFF);			/* past the curve */

	gfx_fill(&fb, 0, 0, W, H, 0x000000);
	gfx_disc16(&fb, 400 * 16, 400 * 16, 20 * 16, 0xFFFFFF, 255);
	CHECK(at(400, 400) == 0xFFFFFF && at(390, 400) == 0xFFFFFF);
	CHECK(at(425, 400) == 0 && at(400, 375) == 0);
	CHECK(at(419, 400) != 0);			/* inside the radius */

	gfx_fill(&fb, 0, 0, W, H, 0x000000);
	gfx_ring16(&fb, 400 * 16, 400 * 16, 20 * 16, 3 * 16, 0xFFFFFF, 255);
	CHECK(at(400, 400) == 0);			/* hollow */
	CHECK(at(419, 400) != 0);			/* on the ring */

	gfx_fill(&fb, 0, 0, W, H, 0x000000);
	gfx_vgrad(&fb, 0, 0, 10, 101, 0x000000, 0xFF0000);
	CHECK(GFX_R(at(5, 0)) == 0 && GFX_R(at(5, 100)) == 255);
	CHECK(GFX_R(at(5, 50)) > 120 && GFX_R(at(5, 50)) < 135);
}

static void test_text(void)
{
	int w, x, y, lit = 0;

	printf("-- text\n");
	gfx_fill(&fb, 0, 0, W, H, 0x000000);
	w = gfx_text(&fb, &gfx_font_ui, 10, 10, "Hello", 0xFFFFFF);
	CHECK(w == gfx_text_width(&gfx_font_ui, "Hello") && w > 20);
	for (y = 10; y < 28; y++)
		for (x = 10; x < 10 + w; x++)
			if (at(x, y))
				lit++;
	CHECK(lit > 30);
	CHECK(gfx_text_width(&gfx_font_cell, "abc") == 36);
	CHECK(gfx_text_width(&gfx_font_uib, "W") > gfx_text_width(&gfx_font_uib, "i"));

	/* pieces exist and are not blank */
	{
		int p, i, nz;

		for (p = 0; p < 6; p++) {
			nz = 0;
			for (i = 0; i < GFX_SPRITE * GFX_SPRITE; i++)
				nz += gfx_chess_sprites[p * GFX_SPRITE * GFX_SPRITE + i] > 128;
			CHECK(nz > 300 && nz < GFX_SPRITE * GFX_SPRITE * 2 / 3);
		}
	}
}

static steel_cell_t screen[PIX_COLS * PIX_ROWS];

static void test_pixdev(void)
{
	static struct pixdev d;
	static u32 clientpx[240 * 112];
	static u32 snapshot[W * H];
	int ox, oy, i, diff;

	printf("-- pixdev\n");
	gfx_canvas_init(&fb, W, H, fbmem);
	pixdev_init(&d, &fb);
	ox = d.off_x;
	oy = d.off_y;
	CHECK(ox == 32 && oy == 34);

	for (i = 0; i < PIX_COLS * PIX_ROWS; i++)
		screen[i] = STEEL_CELL(' ', STEEL_ATTR(STEEL_BLACK, STEEL_LGRAY));
	steel_cell_t *row = &screen[0];

	row[0] = STEEL_CELL('A', STEEL_ATTR(STEEL_WHITE, STEEL_BLUE));
	row[1] = STEEL_CELL(0xDB, STEEL_ATTR(STEEL_RED, STEEL_BLACK));
	row[2] = STEEL_CELL(0xC4, STEEL_ATTR(STEEL_BLACK, STEEL_LGRAY));
	row[3] = STEEL_CELL(0xB1, STEEL_ATTR(STEEL_WHITE, STEEL_BLACK));
	pixdev_present(&d, screen);

	CHECK(at(ox + 3, oy + 1) == 0x000080);		/* blue background of 'A' */
	CHECK(at(ox + 12 + 5, oy + 10) == 0x800000);	/* full block in red */
	CHECK(at(ox + 24 + 1, oy + 14) == 0x000000);	/* a horizontal line... */
	CHECK(at(ox + 24 + 1, oy + 5) == 0xC0C0C0);	/* ...only in the middle */
	CHECK(at(ox + 36 + 5, oy + 10) == 0x808080);	/* medium shade, white on black */
	CHECK(at(5, 5) == 0x008080);			/* the border is desktop teal */
	{
		int lit = 0, x, y;

		for (y = 0; y < PIX_CELL_H; y++)
			for (x = 0; x < PIX_CELL_W; x++)
				lit += at(ox + x, oy + y) != 0x000080;
		CHECK(lit > 20);			/* the glyph is drawn */
	}

	/* unchanged cells are not drawn again */
	memcpy(snapshot, fbmem, sizeof(snapshot));
	fbmem[(oy + 100) * W + ox + 100] = 0xABCDEF;	/* scribble on a clean cell */
	pixdev_present(&d, screen);
	CHECK(fbmem[(oy + 100) * W + ox + 100] == 0xABCDEF);
	fbmem[(oy + 100) * W + ox + 100] = snapshot[(oy + 100) * W + ox + 100];

	/* a changed cell is */
	screen[5] = STEEL_CELL('Z', STEEL_ATTR(STEEL_WHITE, STEEL_RED));
	pixdev_present(&d, screen);
	CHECK(at(ox + 5 * 12 + 1, oy + 1) == 0x800000);

	/* the pointer is restored when it moves away */
	memcpy(snapshot, fbmem, sizeof(snapshot));
	pixdev_cursor(&d, 300, 300, true);
	diff = 0;
	for (i = 0; i < W * H; i++)
		diff += fbmem[i] != snapshot[i];
	CHECK(diff > 40 && diff < PIX_CUR_W * PIX_CUR_H);
	pixdev_cursor(&d, 500, 400, true);
	pixdev_cursor(&d, 0, 0, false);
	diff = 0;
	for (i = 0; i < W * H; i++)
		diff += fbmem[i] != snapshot[i];
	CHECK(diff == 0);

	/* pointer at the screen edge must not write outside the framebuffer */
	pixdev_cursor(&d, W - 3, H - 3, true);
	pixdev_cursor(&d, -5, -5, true);
	pixdev_cursor(&d, 0, 0, false);
	diff = 0;
	for (i = 0; i < W * H; i++)
		diff += fbmem[i] != snapshot[i];
	CHECK(diff == 0);

	/* a pixel client shows through hole cells and honours dirty rectangles */
	for (i = 0; i < 240 * 112; i++)
		clientpx[i] = 0x123456;
	CHECK(pixdev_client_set(&d, 0, ox + 12 * 10, oy + 28 * 5, 240, 112,
				clientpx, 240) == 0);
	{
		int cx, cy;

		for (cy = 5; cy < 9; cy++)
			for (cx = 10; cx < 30; cx++)
				screen[cy * PIX_COLS + cx] = STEEL_HOLE(0);
	}
	pixdev_present(&d, screen);
	CHECK(at(ox + 12 * 10 + 3, oy + 28 * 5 + 3) == 0x123456);
	CHECK(at(ox + 12 * 29 + 11, oy + 28 * 8 + 27) == 0x123456);

	clientpx[0] = 0xFFFFFF;				/* change without telling steel */
	pixdev_present(&d, screen);
	CHECK(at(ox + 12 * 10, oy + 28 * 5) == 0x123456);
	pixdev_client_dirty(&d, 0, 0, 0, 1, 1);
	pixdev_present(&d, screen);
	CHECK(at(ox + 12 * 10, oy + 28 * 5) == 0xFFFFFF);

	/* a window drawn over part of the client covers it */
	screen[6 * PIX_COLS + 12] = STEEL_CELL('W', STEEL_ATTR(STEEL_BLACK, STEEL_WHITE));
	pixdev_present(&d, screen);
	CHECK(at(ox + 12 * 12 + 1, oy + 28 * 6 + 1) == 0xFFFFFF);

	/* clients out of range are refused */
	CHECK(pixdev_client_set(&d, 9, 0, 0, 10, 10, clientpx, 10) == -EINVAL);
	CHECK(pixdev_client_set(&d, 0, 0, 0, 0, 10, clientpx, 10) == -EINVAL);
}

int main(void)
{
	printf("test_gfx\n");
	test_basics();
	test_shapes();
	test_text();
	test_pixdev();
	TEST_DONE();
}
