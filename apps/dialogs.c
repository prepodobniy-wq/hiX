// SPDX-License-Identifier: GPL-2.0-only
/*
 * desk: dialog boxes. A message box, an OK/Cancel question and a one-line
 * text prompt, all the same program in different clothes.
 *
 * The callers fill in a request, launch the window and leave; the window
 * copies the request into its own private memory, so any number of dialogs
 * can be open at once. A dialog is modal: the desktop routes every event to
 * it until it closes.
 */
#include <hix/kernel.h>
#include <desk/desk.h>

enum { DLG_MSG, DLG_CONFIRM, DLG_PROMPT };

#define DLG_TEXT	200
#define DLG_INPUT	64

struct dlg {
	int kind;
	int owner;			/* id of the window that asked, or 0 */
	int focus;			/* 0 = OK, 1 = Cancel */
	int len;
	char text[DLG_TEXT];
	char label[40];
	char input[DLG_INPUT];
	void (*ok)(struct win *w);
	void (*done)(struct win *w, const char *text);
};

static struct dlg req;			/* handed to open() by show() */
static char req_title[40];

static const struct app app_dialog;

static int count_lines(const char *s, int *widest)
{
	int n = 1, cur = 0;

	*widest = 0;
	for (; *s; s++) {
		if (*s == '\n') {
			n++;
			cur = 0;
		} else {
			cur++;
			*widest = max(*widest, cur);
		}
	}
	return n;
}

static int nbuttons(const struct dlg *d)
{
	return d->kind == DLG_MSG ? 1 : 2;
}

static const char *button_label(int i)
{
	return i == 0 ? "OK" : "Cancel";
}

/* where button @i sits in the client area */
static void button_pos(const struct dlg *d, int cw, int ch, int i, int *x,
		       int *y, int *w)
{
	const int gap = 3;
	int n = nbuttons(d), total = 0, k;

	for (k = 0; k < n; k++)
		total += (int)strlen(button_label(k)) + 4;
	total += gap * (n - 1);
	*x = (cw - total) / 2;
	for (k = 0; k < i; k++)
		*x += (int)strlen(button_label(k)) + 4 + gap;
	*y = ch - 2;
	*w = (int)strlen(button_label(i)) + 4;
}

static void show(int kind, struct win *owner, const char *title,
		 const char *text, const char *label)
{
	memset(&req, 0, sizeof(req));
	req.kind = kind;
	req.owner = owner ? owner->id : 0;
	strlcpy(req.text, text, sizeof(req.text));
	if (label)
		strlcpy(req.label, label, sizeof(req.label));
	strlcpy(req_title, title, sizeof(req_title));
}

static void launch_req(void)
{
	int lines, widest, cw, ch;

	lines = count_lines(req.text, &widest);
	cw = max(widest, req.kind == DLG_MSG ? 14 : 22) + 6;
	if (req.kind == DLG_PROMPT)
		cw = max(cw, 40);
	ch = lines + 4 + (req.kind == DLG_PROMPT ? 2 : 0);
	desk_launch_sized(&app_dialog, NULL, cw, ch);
}

void desk_msgbox(const char *title, const char *text)
{
	show(DLG_MSG, NULL, title, text, NULL);
	launch_req();
}

void desk_confirm(struct win *w, const char *title, const char *text,
		  void (*ok)(struct win *w))
{
	show(DLG_CONFIRM, w, title, text, NULL);
	req.ok = ok;
	launch_req();
}

void desk_prompt(struct win *w, const char *title, const char *label,
		 const char *initial,
		 void (*done)(struct win *w, const char *text))
{
	show(DLG_PROMPT, w, title, "", label);
	req.done = done;
	if (initial)
		strlcpy(req.input, initial, sizeof(req.input));
	req.len = (int)strlen(req.input);
	/* the label is the text: it is what sets the dialog's width */
	strlcpy(req.text, label, sizeof(req.text));
	launch_req();
}

/* ---- the program ----------------------------------------------------- */

static void dlg_open(struct win *w, const char *arg)
{
	struct dlg *d = DESK_PRIV(w, struct dlg);

	(void)arg;
	*d = req;
	desk_set_title(w, req_title);
	w->modal = true;
}

static void finish(struct win *w, bool ok)
{
	struct dlg *d = DESK_PRIV(w, struct dlg);
	struct win *owner = d->owner ? desk_find(d->owner) : NULL;
	char text[DLG_INPUT];
	int kind = d->kind;
	void (*ok_cb)(struct win *) = d->ok;
	void (*done_cb)(struct win *, const char *) = d->done;
	bool gone = d->owner && !owner;

	strlcpy(text, d->input, sizeof(text));
	desk_close(w);
	if (!ok || gone)
		return;
	if (kind == DLG_CONFIRM && ok_cb)
		ok_cb(owner);
	else if (kind == DLG_PROMPT && done_cb)
		done_cb(owner, text);
}

static void dlg_paint(struct win *w, struct steel_surface *c)
{
	struct dlg *d = DESK_PRIV(w, struct dlg);
	int row = 1, i, bx, by, bw;
	const char *p = d->text;

	steel_fill(c, ' ', A_GRAY);
	while (*p) {
		char line[DLG_TEXT];
		int n = 0;

		while (p[n] && p[n] != '\n' && n < (int)sizeof(line) - 1) {
			line[n] = p[n];
			n++;
		}
		line[n] = '\0';
		steel_puts(c, 3, row, line, A_GRAY);
		row++;
		p += n;
		if (*p == '\n')
			p++;
	}
	if (d->kind == DLG_PROMPT) {
		int fw = c->w - 6;
		int shown = d->len > fw - 2 ? d->len - (fw - 2) : 0;

		steel_rect(c, 3, row, fw, 1, ' ', A_WIN);
		steel_puts(c, 3, row, d->input + shown, A_WIN);
		steel_put(c, 3 + d->len - shown, row, '_', A_WIN);
	}
	for (i = 0; i < nbuttons(d); i++) {
		button_pos(d, c->w, c->h, i, &bx, &by, &bw);
		desk_draw_button(c, bx, by, button_label(i), d->focus == i);
	}
}

static bool dlg_key(struct win *w, int key, u8 mods)
{
	struct dlg *d = DESK_PRIV(w, struct dlg);

	(void)mods;
	switch (key) {
	case '\n':
		finish(w, d->kind == DLG_MSG || d->focus == 0);
		return true;
	case 27:
		finish(w, false);
		return true;
	case '\t':
	case KEY_LEFT:
	case KEY_RIGHT:
		if (nbuttons(d) > 1)
			d->focus ^= 1;
		return true;
	case 8:
	case 127:
		if (d->kind == DLG_PROMPT && d->len > 0)
			d->input[--d->len] = '\0';
		return true;
	default:
		break;
	}
	if (d->kind == DLG_PROMPT && key >= 0x20 && key < 0x7f &&
	    d->len < DLG_INPUT - 1) {
		d->input[d->len++] = (char)key;
		d->input[d->len] = '\0';
		return true;
	}
	if (d->kind != DLG_PROMPT && key == ' ') {
		finish(w, d->kind == DLG_MSG || d->focus == 0);
		return true;
	}
	return true;			/* modal: swallow everything else */
}

static void dlg_mouse(struct win *w, int ev, int x, int y, int buttons)
{
	struct dlg *d = DESK_PRIV(w, struct dlg);
	int i, bx, by, bw;

	if (ev != MEV_DOWN || !(buttons & MOUSE_LEFT))
		return;
	for (i = 0; i < nbuttons(d); i++) {
		button_pos(d, w->client.w, w->client.h, i, &bx, &by, &bw);
		if (y == by && x >= bx && x < bx + bw) {
			d->focus = i;
			finish(w, d->kind == DLG_MSG || i == 0);
			return;
		}
	}
}

static const struct app app_dialog = {
	.name = "dialog",
	.title = "hix Desktop",
	.icon = { "      ", "      " },
	.cw = 30, .ch = 6,
	.flags = APP_DIALOG,
	.open = dlg_open,
	.paint = dlg_paint,
	.key = dlg_key,
	.mouse = dlg_mouse,
};
