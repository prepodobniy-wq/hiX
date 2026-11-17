// SPDX-License-Identifier: GPL-2.0-only
/* steel fb: pick the framebuffer adapter, see fb.h. */
#include <hix/kernel.h>
#include <steel/fb.h>

enum fb_kind {
	FB_NONE,
	FB_BGA,
	FB_VMWARE,
};

static enum fb_kind active;

static enum fb_kind present_kind(void)
{
	if (bga_present())
		return FB_BGA;
	if (vmware_present())
		return FB_VMWARE;
	return FB_NONE;
}

bool steel_fb_present(void)
{
	return present_kind() != FB_NONE;
}

const char *steel_fb_name(void)
{
	switch (active != FB_NONE ? active : present_kind()) {
	case FB_BGA:
		return "bochs vbe";
	case FB_VMWARE:
		return "vmware svga";
	default:
		return "none";
	}
}

u32 *steel_fb_enable(int w, int h)
{
	u32 *mem;

	if (bga_present()) {
		mem = bga_enable(w, h);
		if (mem)
			active = FB_BGA;
		return mem;
	}
	if (vmware_present()) {
		mem = vmware_enable(w, h);
		if (mem)
			active = FB_VMWARE;
		return mem;
	}
	return NULL;
}

void steel_fb_disable(void)
{
	if (active == FB_BGA)
		bga_disable();
	else if (active == FB_VMWARE)
		vmware_disable();
	active = FB_NONE;
}

void steel_fb_update(int x, int y, int w, int h)
{
	if (active == FB_VMWARE)
		vmware_update(x, y, w, h);
}
