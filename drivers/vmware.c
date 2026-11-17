// SPDX-License-Identifier: GPL-2.0-only
/*
 * VMware SVGA II adapter, found in VMware Workstation/Fusion/ESXi and in QEMU
 * (-vga vmware, device vmware-svga).
 *
 * BAR0 is an I/O window: write a register index, then read or write the value.
 * BAR1 is the linear framebuffer. The command FIFO lives at SVGA_REG_MEM_START;
 * the host does not scan the framebuffer by itself, so every changed rectangle
 * is announced with an UPDATE command. On hardware without the adapter the
 * driver reports absent and steel stays in text mode.
 */
#define pr_fmt(fmt) "vmware: " fmt

#include <hix/kernel.h>
#include <hix/driver.h>
#include <hix/pci.h>
#include <asm/io.h>
#include <steel/fb.h>

#define SVGA_VENDOR		0x15AD
#define SVGA_DEVICE		0x0405

/* register indices, VMware SVGA II programmer's guide */
#define SVGA_REG_ID		0
#define SVGA_REG_ENABLE		1
#define SVGA_REG_WIDTH		2
#define SVGA_REG_HEIGHT		3
#define SVGA_REG_MAX_WIDTH	4
#define SVGA_REG_MAX_HEIGHT	5
#define SVGA_REG_BITS_PER_PIXEL	7
#define SVGA_REG_BYTES_PER_LINE	12
#define SVGA_REG_FB_START	13
#define SVGA_REG_FB_OFFSET	14
#define SVGA_REG_FB_SIZE	16
#define SVGA_REG_MEM_START	18
#define SVGA_REG_MEM_SIZE	19
#define SVGA_REG_CONFIG_DONE	20
#define SVGA_REG_SYNC		21
#define SVGA_REG_BUSY		22

#define SVGA_ID_2		0x90000002
#define SVGA_ID_MASK		0xFFFFFF00

/* FIFO offsets, in 32-bit words */
#define SVGA_FIFO_MIN		0
#define SVGA_FIFO_MAX		1
#define SVGA_FIFO_NEXT		2
#define SVGA_FIFO_STOP		3

#define SVGA_FIFO_RESERVED	16	/* bytes before the first command */
#define SVGA_CMD_UPDATE		1
#define SVGA_UPDATE_WORDS	5	/* command and four arguments */

static u16 io_base;
static u32 fb_phys;
static u32 fifo_phys, fifo_size;
static u32 mode_w, mode_h;
static bool found, enabled;

static u32 svga_read(u32 index)
{
	outl(io_base, index);
	return inl(io_base + 1);
}

static void svga_write(u32 index, u32 value)
{
	outl(io_base, index);
	outl(io_base + 1, value);
}

static int vmware_probe(void)
{
	int bus, dev, fn;
	u32 bar0, bar1, cmd, id, fb_start, fb_offset;

	if (!pci_find(SVGA_VENDOR, SVGA_DEVICE, &bus, &dev, &fn))
		return -ENODEV;

	bar0 = pci_read32(bus, dev, fn, 0x10);
	bar1 = pci_read32(bus, dev, fn, 0x14);
	if (!(bar0 & 0x1) || (bar1 & 0x1) || !(bar1 & ~0xFu))
		return -ENODEV;

	io_base = (u16)(bar0 & ~0x3u);
	fb_phys = bar1 & ~0xFu;
	cmd = pci_read32(bus, dev, fn, 0x04);
	pci_write32(bus, dev, fn, 0x04, cmd | 0x3);	/* I/O and memory */

	svga_write(SVGA_REG_ID, SVGA_ID_2);
	id = svga_read(SVGA_REG_ID);
	if ((id & SVGA_ID_MASK) != (SVGA_ID_2 & SVGA_ID_MASK)) {
		pr_warn("%02x:%02x.%x has no SVGA II id (%#x)\n", bus, dev, fn,
			id);
		return -ENODEV;
	}

	fb_start = svga_read(SVGA_REG_FB_START);
	fb_offset = svga_read(SVGA_REG_FB_OFFSET);
	fifo_phys = svga_read(SVGA_REG_MEM_START);
	fifo_size = svga_read(SVGA_REG_MEM_SIZE);
	if (!fb_start)
		fb_start = fb_phys;
	if (!fifo_phys || fifo_size < SVGA_FIFO_RESERVED + 4096 ||
	    fifo_phys > 0xFFFFFFFFu - fifo_size) {
		pr_warn("no usable command FIFO (start %#x, size %#x)\n",
			fifo_phys, fifo_size);
		return -ENODEV;
	}

	fb_phys = fb_start + fb_offset;
	found = true;
	pr_info("adapter %04x:%04x at %02x:%02x.%x, framebuffer %#x, "
		"fifo %#x (%u KiB)\n", SVGA_VENDOR, SVGA_DEVICE, bus, dev, fn,
		fb_phys, fifo_phys, fifo_size >> 10);
	return 0;
}

static int vmware_start(void)
{
	return 0;
}

bool vmware_present(void)
{
	return found;
}

/* one command word; @pos and the FIFO min/max are byte offsets */
static u32 fifo_put(u32 *fifo, u32 pos, u32 value)
{
	fifo[pos >> 2] = value;
	pos += 4;
	if (pos >= fifo[SVGA_FIFO_MAX])
		pos = fifo[SVGA_FIFO_MIN];
	return pos;
}

void vmware_update(int x, int y, int w, int h)
{
	u32 *fifo = (u32 *)(unsigned long)fifo_phys;
	u32 min, max, next, stop, used, space, i;

	if (!enabled || w <= 0 || h <= 0)
		return;
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > (int)mode_w)
		w = (int)mode_w - x;
	if (y + h > (int)mode_h)
		h = (int)mode_h - y;
	if (w <= 0 || h <= 0)
		return;

	min = fifo[SVGA_FIFO_MIN];
	max = fifo[SVGA_FIFO_MAX];
	next = fifo[SVGA_FIFO_NEXT];
	stop = fifo[SVGA_FIFO_STOP];
	if (next < min || next >= max || stop < min || stop >= max)
		return;
	used = next >= stop ? next - stop : (max - next) + (stop - min);
	space = (max - min) - used;
	if (space < SVGA_UPDATE_WORDS * 4) {
		/* let the host drain what is already queued, then retry once */
		svga_write(SVGA_REG_SYNC, 1);
		for (i = 0; i < 1000000 && svga_read(SVGA_REG_BUSY); i++)
			barrier();
		next = fifo[SVGA_FIFO_NEXT];
		stop = fifo[SVGA_FIFO_STOP];
		used = next >= stop ? next - stop :
				      (max - next) + (stop - min);
		if (next < min || next >= max || stop < min || stop >= max ||
		    (max - min) - used < SVGA_UPDATE_WORDS * 4)
			return;
	}

	next = fifo_put(fifo, next, SVGA_CMD_UPDATE);
	next = fifo_put(fifo, next, (u32)x);
	next = fifo_put(fifo, next, (u32)y);
	next = fifo_put(fifo, next, (u32)w);
	next = fifo_put(fifo, next, (u32)h);
	fifo[SVGA_FIFO_NEXT] = next;
	barrier();
	svga_write(SVGA_REG_SYNC, 1);
	while (svga_read(SVGA_REG_BUSY))
		barrier();
}

u32 *vmware_enable(int w, int h)
{
	u32 *fifo, max_w, max_h, pitch;

	if (!found)
		return NULL;
	if (enabled)
		return (u32 *)(unsigned long)fb_phys;
	if (w <= 0 || h <= 0)
		return NULL;

	max_w = svga_read(SVGA_REG_MAX_WIDTH);
	max_h = svga_read(SVGA_REG_MAX_HEIGHT);
	if (max_w < (u32)w || max_h < (u32)h) {
		pr_warn("the adapter is limited to %ux%u, not %dx%d\n",
			max_w, max_h, w, h);
		return NULL;
	}

	vga_font_save();		/* the framebuffer will overwrite it */
	svga_write(SVGA_REG_ENABLE, 0);
	svga_write(SVGA_REG_WIDTH, (u32)w);
	svga_write(SVGA_REG_HEIGHT, (u32)h);
	svga_write(SVGA_REG_BITS_PER_PIXEL, 32);
	svga_write(SVGA_REG_ENABLE, 1);
	if (svga_read(SVGA_REG_WIDTH) != (u32)w ||
	    svga_read(SVGA_REG_HEIGHT) != (u32)h ||
	    svga_read(SVGA_REG_BITS_PER_PIXEL) != 32) {
		pr_warn("the adapter refused %dx32\n", w);
		goto fail;
	}
	pitch = svga_read(SVGA_REG_BYTES_PER_LINE);
	if (pitch != (u32)w * 4) {
		pr_warn("unexpected pitch %u for %d pixels\n", pitch, w);
		goto fail;
	}

	fifo = (u32 *)(unsigned long)fifo_phys;
	memset(fifo, 0, SVGA_FIFO_RESERVED);
	fifo[SVGA_FIFO_MIN] = SVGA_FIFO_RESERVED;
	fifo[SVGA_FIFO_MAX] = fifo_size;
	fifo[SVGA_FIFO_NEXT] = SVGA_FIFO_RESERVED;
	fifo[SVGA_FIFO_STOP] = SVGA_FIFO_RESERVED;
	barrier();
	svga_write(SVGA_REG_CONFIG_DONE, 1);

	mode_w = (u32)w;
	mode_h = (u32)h;
	enabled = true;
	return (u32 *)(unsigned long)fb_phys;

fail:
	svga_write(SVGA_REG_ENABLE, 0);
	vga_text_restore();
	return NULL;
}

void vmware_disable(void)
{
	if (!enabled)
		return;
	enabled = false;
	svga_write(SVGA_REG_ENABLE, 0);
	svga_write(SVGA_REG_CONFIG_DONE, 0);
	vga_text_restore();		/* the pixel mode left VGA registers changed */
}

static struct hix_driver vmware_driver = {
	.name = "vmware",
	.kind = "display",
	.level = DRV_LEVEL_PLATFORM,
	.probe = vmware_probe,
	.start = vmware_start,
};
HIX_DRIVER(vmware_driver);
