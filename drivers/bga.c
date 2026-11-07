// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bochs VBE adapter ("bga"), found in QEMU (-vga std, the default), Bochs and
 * VirtualBox. It sets a graphics mode through two I/O ports and exposes the
 * framebuffer as PCI BAR 0, so no BIOS call is needed. On hardware without
 * it the driver reports absent and steel stays in text mode.
 */
#define pr_fmt(fmt) "bga: " fmt

#include <hix/kernel.h>
#include <hix/driver.h>
#include <hix/pci.h>
#include <asm/io.h>
#include <steel/bga.h>

#define VBE_INDEX	0x01CE
#define VBE_DATA	0x01CF

#define VBE_ID		0
#define VBE_XRES	1
#define VBE_YRES	2
#define VBE_BPP		3
#define VBE_ENABLE	4
#define VBE_BANK	5
#define VBE_VIRT_W	6
#define VBE_VIRT_H	7
#define VBE_XOFF	8
#define VBE_YOFF	9

#define VBE_ID_MIN	0xB0C0
#define VBE_ID_MAX	0xB0C5
#define VBE_DISABLED	0x00
#define VBE_ENABLED	0x01
#define VBE_LFB		0x40

static u32 fb_phys;
static bool found;

static void vbe_write(u16 idx, u16 val)
{
	outw(VBE_INDEX, idx);
	outw(VBE_DATA, val);
}

static u16 vbe_read(u16 idx)
{
	outw(VBE_INDEX, idx);
	return inw(VBE_DATA);
}

static int bga_probe(void)
{
	static const struct { u16 vendor, device; } ids[] = {
		{ 0x1234, 0x1111 },	/* QEMU, Bochs */
		{ 0x80EE, 0xBEEF },	/* VirtualBox */
	};
	unsigned int i;
	u16 id;

	for (i = 0; i < ARRAY_SIZE(ids); i++) {
		int bus, dev, fn;

		if (!pci_find(ids[i].vendor, ids[i].device, &bus, &dev, &fn))
			continue;
		id = vbe_read(VBE_ID);
		if (id < VBE_ID_MIN || id > VBE_ID_MAX)
			continue;
		fb_phys = pci_read32(bus, dev, fn, 0x10) & ~0xFu;
		if (!fb_phys)
			continue;
		found = true;
		pr_info("adapter %04x:%04x at %02x:%02x.%x, VBE 0x%04x, "
			"framebuffer %#x\n", ids[i].vendor, ids[i].device, bus,
			dev, fn, id, fb_phys);
		return 0;
	}
	return -ENODEV;
}

static int bga_start(void)
{
	return 0;
}

bool bga_present(void)
{
	return found;
}

u32 *bga_enable(int w, int h)
{
	if (!found)
		return NULL;
	vga_font_save();		/* the framebuffer will overwrite it */
	vbe_write(VBE_ENABLE, VBE_DISABLED);
	vbe_write(VBE_XRES, (u16)w);
	vbe_write(VBE_YRES, (u16)h);
	vbe_write(VBE_BPP, 32);
	vbe_write(VBE_VIRT_W, (u16)w);
	vbe_write(VBE_VIRT_H, (u16)h);	/* no scrolling room: the picture is the screen */
	vbe_write(VBE_XOFF, 0);
	vbe_write(VBE_YOFF, 0);
	vbe_write(VBE_ENABLE, VBE_ENABLED | VBE_LFB);
	if (vbe_read(VBE_XRES) != w || vbe_read(VBE_YRES) != h) {
		pr_warn("the adapter refused %dx%d\n", w, h);
		vbe_write(VBE_ENABLE, VBE_DISABLED);
		vga_text_restore();
		return NULL;
	}
	return (u32 *)(unsigned long)fb_phys;
}

void bga_disable(void)
{
	if (!found)
		return;
	vbe_write(VBE_ENABLE, VBE_DISABLED);
	vga_text_restore();		/* the pixel mode left the VGA registers changed */
}

static struct hix_driver bga_driver = {
	.name = "bga",
	.class = "display",
	.level = DRV_LEVEL_PLATFORM,
	.probe = bga_probe,
	.start = bga_start,
};
HIX_DRIVER(bga_driver);
