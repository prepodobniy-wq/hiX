// SPDX-License-Identifier: GPL-2.0-only
/*
 * steel: the compositor. Layers are windows; the last one in the list is on
 * top. steel_compose() paints the desktop, a menu bar, and then every window
 * with frame, title and drop shadow, back to front.
 */
#include <hix/kernel.h>
#include <aether/kalloc.h>
#include <steel/steel.h>

#define DESKTOP_ATTR	STEEL_ATTR(STEEL_BLUE, STEEL_CYAN)
#define MENU_ATTR	STEEL_ATTR(STEEL_BLACK, STEEL_LGRAY)
#define SHADOW_ATTR	STEEL_ATTR(STEEL_DGRAY, STEEL_BLACK)

static LIST_HEAD(layers);

struct steel_layer *steel_layer_create(int x, int y, int content_w,
				       int content_h, const char *title)
{
	struct steel_layer *l = kzalloc(sizeof(*l));

	if (!l)
		return NULL;
	l->content = steel_surface_create(content_w, content_h);
	if (!l->content) {
		kfree(l);
		return NULL;
	}
	l->x = x;
	l->y = y;
	l->frame_attr = STEEL_ATTR(STEEL_WHITE, STEEL_BLUE);
	strlcpy(l->title, title, sizeof(l->title));
	steel_fill(l->content, ' ', STEEL_ATTR(STEEL_BLACK, STEEL_LGRAY));
	list_add_tail(&l->node, &layers);
	return l;
}

void steel_layer_destroy(struct steel_layer *layer)
{
	list_del(&layer->node);
	steel_surface_destroy(layer->content);
	kfree(layer);
}

void steel_layer_raise(struct steel_layer *layer)
{
	list_del(&layer->node);
	list_add_tail(&layer->node, &layers);
}

void steel_layer_move(struct steel_layer *layer, int x, int y)
{
	layer->x = x;
	layer->y = y;
}

static void draw_layer(struct steel_surface *dest, const struct steel_layer *l,
		       bool active)
{
	int fw = l->content->w + 2, fh = l->content->h + 2;
	int tlen = (int)strlen(l->title);
	u8 attr = active ? l->frame_attr
			 : STEEL_ATTR(STEEL_LGRAY, STEEL_BLUE);

	/* shadow first: darken what lies right of and below the window */
	steel_tint(dest, l->x + fw, l->y + 1, 2, fh, SHADOW_ATTR);
	steel_tint(dest, l->x + 2, l->y + fh, fw, 1, SHADOW_ATTR);

	steel_blit(dest, l->x + 1, l->y + 1, l->content, 0, 0,
		   l->content->w, l->content->h);
	steel_box(dest, l->x, l->y, fw, fh, STEEL_BOX_DOUBLE, attr);

	if (tlen > fw - 6)
		tlen = fw - 6;
	if (tlen > 0) {
		int tx = l->x + (fw - tlen - 2) / 2;

		steel_put(dest, tx, l->y, ' ', attr);
		steel_rect(dest, tx + 1, l->y, tlen, 1, ' ', attr);
		{
			char tmp[24];

			strlcpy(tmp, l->title, tlen + 1);
			steel_puts(dest, tx + 1, l->y, tmp,
				   STEEL_ATTR(STEEL_YELLOW, STEEL_BLUE));
		}
		steel_put(dest, tx + tlen + 1, l->y, ' ', attr);
	}
}

void steel_compose(struct steel_surface *dest, const char *menubar_text)
{
	struct steel_layer *l;

	steel_fill(dest, STEEL_SHADE_LIGHT, DESKTOP_ATTR);
	steel_rect(dest, 0, 0, dest->w, 1, ' ', MENU_ATTR);
	if (menubar_text)
		steel_puts(dest, 1, 0, menubar_text, MENU_ATTR);

	list_for_each_entry(l, &layers, node)
		draw_layer(dest, l, l->node.next == &layers);
}
