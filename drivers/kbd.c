// SPDX-License-Identifier: GPL-2.0-only
/*
 * PS/2 keyboard, scan code set 1, US layout. Reports keys to qubix input,
 * which sends them to the tty or, for a full-screen program, to its event
 * queue.
 */
#define pr_fmt(fmt) "kbd: " fmt

#include <hix/kernel.h>
#include <hix/driver.h>
#include <asm/io.h>
#include <asm/trap.h>
#include <qubix/input.h>

#define KBD_DATA	0x60
#define KBD_STATUS	0x64
#define KBD_IRQ		1

static const char keymap[58] =
	"\0\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
static const char keymap_shift[58] =
	"\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";

static bool shift, ctrl, alt, caps, extended;

static u8 mods(void)
{
	return (shift ? MOD_SHIFT : 0) | (ctrl ? MOD_CTRL : 0) |
	       (alt ? MOD_ALT : 0);
}

/* the cursor block works with and without NumLock: keypad keys navigate */
static int nav_key(u8 code)
{
	switch (code) {
	case 0x48: return KEY_UP;
	case 0x50: return KEY_DOWN;
	case 0x4B: return KEY_LEFT;
	case 0x4D: return KEY_RIGHT;
	case 0x47: return KEY_HOME;
	case 0x4F: return KEY_END;
	case 0x49: return KEY_PGUP;
	case 0x51: return KEY_PGDN;
	case 0x52: return KEY_INS;
	case 0x53: return KEY_DEL;
	default:   return 0;
	}
}

static void handle_scancode(u8 code, bool ext)
{
	bool release = code & 0x80;
	int c;

	code &= 0x7F;

	/* modifiers: the right-hand Ctrl and Alt arrive as extended codes */
	if (code == 0x1D) {
		ctrl = !release;
		return;
	}
	if (code == 0x38) {
		alt = !release;
		return;
	}
	if (!ext && (code == 0x2A || code == 0x36)) {
		shift = !release;
		return;
	}
	if (release)
		return;
	if (!ext && code == 0x3A) {
		caps = !caps;
		return;
	}

	if (ext) {
		if (code == 0x1C)
			input_key('\n', mods());	/* keypad enter */
		else if (code == 0x35)
			input_key('/', mods());		/* keypad slash */
		else if ((c = nav_key(code)))
			input_key(c, mods());
		return;
	}

	if (code >= 0x3B && code <= 0x44) {		/* F1 .. F10 */
		input_key(KEY_F(code - 0x3B + 1), mods());
		return;
	}
	if (code == 0x57 || code == 0x58) {		/* F11, F12 */
		input_key(KEY_F(code - 0x57 + 11), mods());
		return;
	}
	if (code >= 0x47 && code != 0x4A && code != 0x4E && code <= 0x53) {
		if ((c = nav_key(code)))		/* keypad as cursor keys */
			input_key(c, mods());
		return;
	}

	if (code >= sizeof(keymap))
		return;
	c = shift ? keymap_shift[code] : keymap[code];
	if (!c)
		return;
	if (caps && c >= 'a' && c <= 'z' && !shift)
		c -= 32;
	else if (caps && c >= 'A' && c <= 'Z' && shift)
		c += 32;
	input_key(c, mods());
}

static void kbd_irq(int irq, void *dev)
{
	u8 code = inb(KBD_DATA);
	bool ext = extended;

	(void)irq;
	(void)dev;
	if (code == 0xE0) {
		extended = true;
		return;
	}
	extended = false;
	handle_scancode(code, ext);
}

static int kbd_probe(void)
{
	return inb(KBD_STATUS) == 0xFF ? -ENODEV : 0;
}

static int kbd_start(void)
{
	int guard = 64;

	while ((inb(KBD_STATUS) & 1) && guard--)	/* drain stale bytes */
		inb(KBD_DATA);
	return request_irq(KBD_IRQ, kbd_irq, "keyboard", NULL);
}

static struct hix_driver kbd_driver = {
	.name = "ps2kbd",
	.class = "input",
	.level = DRV_LEVEL_INPUT,
	.probe = kbd_probe,
	.start = kbd_start,
};
HIX_DRIVER(kbd_driver);
