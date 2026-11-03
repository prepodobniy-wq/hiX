// SPDX-License-Identifier: GPL-2.0-only
/* steel: display device registry and command buffers. */
#include <hix/kernel.h>
#include <steel/steel.h>

static struct steel_device *main_device;

void steel_set_device(struct steel_device *dev)
{
	main_device = dev;
}

struct steel_device *steel_get_device(void)
{
	return main_device;
}

void steel_present(const struct steel_surface *s)
{
	if (main_device && main_device->present)
		main_device->present(main_device, s);
}

void steel_set_cursor(int x, int y, bool visible)
{
	if (main_device && main_device->set_cursor)
		main_device->set_cursor(main_device, x, y, visible);
}

/* ---- command buffers ------------------------------------------------- */

void steel_cmdbuf_begin(struct steel_cmdbuf *cb, struct steel_surface *target)
{
	cb->target = target;
	cb->count = 0;
}

static struct steel_cmd *next_cmd(struct steel_cmdbuf *cb, enum steel_op op)
{
	struct steel_cmd *c;

	if (cb->count >= STEEL_CMDBUF_MAX)
		return NULL;
	c = &cb->cmds[cb->count++];
	memset(c, 0, sizeof(*c));
	c->op = op;
	return c;
}

int steel_enc_clear(struct steel_cmdbuf *cb, u8 ch, u8 attr)
{
	struct steel_cmd *c = next_cmd(cb, STEEL_OP_CLEAR);

	if (!c)
		return -ENOSPC;
	c->ch = ch;
	c->attr = attr;
	return 0;
}

int steel_enc_fill_rect(struct steel_cmdbuf *cb, int x, int y, int w, int h,
			u8 ch, u8 attr)
{
	struct steel_cmd *c = next_cmd(cb, STEEL_OP_FILL_RECT);

	if (!c)
		return -ENOSPC;
	c->x = x;
	c->y = y;
	c->w = w;
	c->h = h;
	c->ch = ch;
	c->attr = attr;
	return 0;
}

int steel_enc_box(struct steel_cmdbuf *cb, int x, int y, int w, int h,
		  enum steel_box_style style, u8 attr)
{
	struct steel_cmd *c = next_cmd(cb, STEEL_OP_BOX);

	if (!c)
		return -ENOSPC;
	c->x = x;
	c->y = y;
	c->w = w;
	c->h = h;
	c->style = style;
	c->attr = attr;
	return 0;
}

int steel_enc_text(struct steel_cmdbuf *cb, int x, int y, const char *text,
		   u8 attr)
{
	struct steel_cmd *c = next_cmd(cb, STEEL_OP_TEXT);

	if (!c)
		return -ENOSPC;
	c->x = x;
	c->y = y;
	c->text = text;
	c->attr = attr;
	return 0;
}

int steel_enc_hline(struct steel_cmdbuf *cb, int x, int y, int len, u8 ch,
		    u8 attr)
{
	struct steel_cmd *c = next_cmd(cb, STEEL_OP_HLINE);

	if (!c)
		return -ENOSPC;
	c->x = x;
	c->y = y;
	c->w = len;
	c->ch = ch;
	c->attr = attr;
	return 0;
}

int steel_enc_tint(struct steel_cmdbuf *cb, int x, int y, int w, int h,
		   u8 attr)
{
	struct steel_cmd *c = next_cmd(cb, STEEL_OP_TINT);

	if (!c)
		return -ENOSPC;
	c->x = x;
	c->y = y;
	c->w = w;
	c->h = h;
	c->attr = attr;
	return 0;
}

int steel_execute(struct steel_cmdbuf *cb)
{
	struct steel_surface *t = cb->target;
	int i;

	for (i = 0; i < cb->count; i++) {
		const struct steel_cmd *c = &cb->cmds[i];

		switch (c->op) {
		case STEEL_OP_CLEAR:
			steel_fill(t, c->ch, c->attr);
			break;
		case STEEL_OP_FILL_RECT:
			steel_rect(t, c->x, c->y, c->w, c->h, c->ch, c->attr);
			break;
		case STEEL_OP_BOX:
			steel_box(t, c->x, c->y, c->w, c->h,
				  (enum steel_box_style)c->style, c->attr);
			break;
		case STEEL_OP_TEXT:
			steel_puts(t, c->x, c->y, c->text, c->attr);
			break;
		case STEEL_OP_HLINE:
			steel_hline(t, c->x, c->y, c->w, c->ch, c->attr);
			break;
		case STEEL_OP_TINT:
			steel_tint(t, c->x, c->y, c->w, c->h, c->attr);
			break;
		}
	}
	return cb->count;
}

int steel_commit(struct steel_cmdbuf *cb)
{
	int n = steel_execute(cb);

	steel_present(cb->target);
	cb->count = 0;
	return n;
}
