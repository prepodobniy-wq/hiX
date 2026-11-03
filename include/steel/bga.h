/* SPDX-License-Identifier: GPL-2.0-only */
/* The Bochs/QEMU display adapter: a linear framebuffer without a BIOS. */
#ifndef _STEEL_BGA_H
#define _STEEL_BGA_H

#include <hix/types.h>

bool bga_present(void);
/* switch to @w x @h at 32 bits; returns the framebuffer or NULL */
u32 *bga_enable(int w, int h);
/* back to the VGA text mode */
void bga_disable(void);

#endif /* _STEEL_BGA_H */
