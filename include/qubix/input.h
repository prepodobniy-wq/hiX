/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * qubix: input events.
 *
 * Keyboard and mouse drivers report here. While nobody owns the display the
 * keys go to the console tty (the shell); a full-screen program such as the
 * desktop grabs the display and then reads typed events instead.
 */
#ifndef _QUBIX_INPUT_H
#define _QUBIX_INPUT_H

#include <hix/types.h>

/* key codes above 7-bit ASCII; printable keys are their ASCII value */
#define KEY_PGUP	0x100
#define KEY_PGDN	0x101
#define KEY_UP		0x102
#define KEY_DOWN	0x103
#define KEY_LEFT	0x104
#define KEY_RIGHT	0x105
#define KEY_HOME	0x106
#define KEY_END		0x107
#define KEY_INS		0x108
#define KEY_DEL		0x109
#define KEY_F1		0x110
#define KEY_F(n)	(KEY_F1 + (n) - 1)	/* F1 .. F12 */

#define MOD_SHIFT	1
#define MOD_CTRL	2
#define MOD_ALT		4

#define MOUSE_LEFT	1
#define MOUSE_RIGHT	2
#define MOUSE_MIDDLE	4

#define INPUT_KEY	1
#define INPUT_MOUSE	2

struct input_event {
	u8 type;		/* INPUT_KEY or INPUT_MOUSE */
	u8 mods;		/* MOD_* (keys) */
	u16 key;		/* INPUT_KEY: ASCII or KEY_* */
	s16 x, y;		/* INPUT_MOUSE: character cell under the pointer */
	s16 px, py;		/* INPUT_MOUSE: pointer position in pixels */
	u8 buttons;		/* INPUT_MOUSE: MOUSE_* bits that are down */
	u8 pad;
};

/* from drivers, interrupt context */
void input_key(int key, u8 mods);
/* relative motion in mickeys (dy positive = down), absolute button state */
void input_mouse(int dx, int dy, u8 buttons);

/* geometry of the pointer space, see input.c */
void input_set_geometry(int space_w, int space_h, int cell_w, int cell_h,
			int off_x, int off_y, bool pixels);
void input_get_pointer(int *x, int *y);

/* display ownership: the owner reads events, everyone else gets the tty */
int input_grab(void);			/* 0 or -EBUSY */
void input_release(void);
bool input_grabbed(void);

/* next event; timeout_ms 0 polls, < 0 waits forever; -ETIMEDOUT if none */
int input_wait(struct input_event *ev, long timeout_ms);

#endif /* _QUBIX_INPUT_H */
