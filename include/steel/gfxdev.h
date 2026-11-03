/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * steel gfxdev: the pixel display as a steel device. While it is active the
 * desktop's 80x25 cell surface is drawn by pixdev, and pixel clients (chess)
 * can register their canvases.
 */
#ifndef _STEEL_GFXDEV_H
#define _STEEL_GFXDEV_H

#include <steel/pixdev.h>

#define GFX_SCREEN_W	1024
#define GFX_SCREEN_H	768

/* 0, or -ENODEV when there is no pixel display */
int steel_gfx_enter(void);
void steel_gfx_leave(void);
bool steel_gfx_active(void);
struct pixdev *steel_gfx_pixdev(void);

#endif /* _STEEL_GFXDEV_H */
