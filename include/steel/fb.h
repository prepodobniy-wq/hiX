/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * steel fb: linear framebuffer adapters.
 *
 * Two paravirtual display adapters are supported: Bochs VBE (QEMU -vga std,
 * Bochs, VirtualBox) and VMware SVGA II (VMware, QEMU -vga vmware). The
 * front-end in steel/fb.c picks the one that probed successfully; the rest of
 * steel only talks to steel_fb_*().
 */
#ifndef _STEEL_FB_H
#define _STEEL_FB_H

#include <hix/types.h>

/* Bochs VBE, drivers/bga.c */
bool bga_present(void);
u32 *bga_enable(int w, int h);
void bga_disable(void);

/* VMware SVGA II, drivers/vmware.c */
bool vmware_present(void);
u32 *vmware_enable(int w, int h);
void vmware_disable(void);
/* tell the host which part of the framebuffer changed */
void vmware_update(int x, int y, int w, int h);

/* the adapter that is present, or the one currently enabled */
bool steel_fb_present(void);
const char *steel_fb_name(void);
u32 *steel_fb_enable(int w, int h);
void steel_fb_disable(void);
/* after pixels were written: bga scans out by itself, VMware needs an update */
void steel_fb_update(int x, int y, int w, int h);

/* text mode support in drivers/vga.c, used around a pixel mode */
void vga_font_save(void);
void vga_text_restore(void);

#endif /* _STEEL_FB_H */
