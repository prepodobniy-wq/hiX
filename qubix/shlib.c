// SPDX-License-Identifier: GPL-2.0-only
/* qubix: the shell's parser, variables, name matching and line editor. */
#include <hix/kernel.h>
#include <qubix/shlib.h>

/* ---- variables ------------------------------------------------------- */

static int var_find(const struct sh_vars *v, const char *name)
{
	int i;

	for (i = 0; i < v->n; i++)
		if (!strcmp(v->name[i], name))
			return i;
	return -1;
}

int sh_var_set(struct sh_vars *v, const char *name, const char *val)
{
	int i = var_find(v, name);

	if (!*name || strlen(name) >= sizeof(v->name[0]))
		return -EINVAL;
	if (i < 0) {
		if (v->n >= SH_VARS_MAX)
			return -ENOSPC;
		i = v->n++;
		strlcpy(v->name[i], name, sizeof(v->name[i]));
	}
	strlcpy(v->val[i], val, sizeof(v->val[i]));
	return 0;
}

const char *sh_var_get(const struct sh_vars *v, const char *name)
{
	int i = var_find(v, name);

	return i < 0 ? NULL : v->val[i];
}

bool sh_var_unset(struct sh_vars *v, const char *name)
{
	int i = var_find(v, name);

	if (i < 0)
		return false;
	v->n--;
	if (i != v->n) {
		memcpy(v->name[i], v->name[v->n], sizeof(v->name[i]));
		memcpy(v->val[i], v->val[v->n], sizeof(v->val[i]));
	}
	return true;
}

/* ---- parser ---------------------------------------------------------- */

struct pstate {
	struct sh_parsed *p;
	const struct sh_vars *v;
	size_t used;
};

static bool is_name_char(char c, bool first)
{
	return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (!first && c >= '0' && c <= '9');
}

static int emit(struct pstate *st, char c)
{
	if (st->used + 1 >= sizeof(st->p->store))
		return -E2BIG;
	st->p->store[st->used++] = c;
	return 0;
}

/* append the value of the variable that starts at *s (after the '$') */
static int expand(struct pstate *st, const char **s)
{
	char name[16];
	const char *val, *q = *s;
	size_t n = 0;
	bool brace = false;
	int err;

	if (*q == '{') {
		brace = true;
		q++;
	}
	if (*q == '?') {
		name[n++] = '?';
		q++;
	} else {
		while (is_name_char(*q, n == 0) && n < sizeof(name) - 1)
			name[n++] = *q++;
	}
	if (brace) {
		if (*q != '}')
			return -EINVAL;
		q++;
	}
	if (!n) {
		/* a lone '$' is an ordinary character */
		return emit(st, '$');
	}
	name[n] = '\0';
	*s = q;
	val = sh_var_get(st->v, name);
	while (val && *val) {
		err = emit(st, *val++);
		if (err)
			return err;
	}
	return 0;
}

static int end_cmd(struct pstate *st, int op, bool *have_cmd)
{
	struct sh_cmd *c = &st->p->cmd[st->p->n];

	if (c->argc == 0)
		return -EINVAL;		/* operator without a command */
	c->argv[c->argc] = NULL;
	c->op = op;
	st->p->n++;
	*have_cmd = false;
	if (st->p->n >= SH_CMDS_MAX && op != SH_END)
		return -E2BIG;
	return 0;
}

int sh_parse(const char *line, struct sh_parsed *p, const struct sh_vars *v)
{
	struct pstate st = { .p = p, .v = v };
	const char *s = line;
	bool in_word = false, have_cmd = false;
	int err;

	memset(p, 0, sizeof(*p));
	while (*s) {
		char c = *s;

		if (c == ' ' || c == '\t' || c == '\n') {
			if (in_word) {
				err = emit(&st, '\0');
				if (err)
					return err;
				in_word = false;
			}
			s++;
			continue;
		}
		if (!in_word && c == '#')
			break;
		if (c == ';' || (c == '&' && s[1] == '&') ||
		    (c == '|' && s[1] == '|')) {
			int op = c == ';' ? SH_SEQ : c == '&' ? SH_AND : SH_OR;

			if (in_word) {	/* the operator ends the word it touches */
				err = emit(&st, '\0');
				if (err)
					return err;
				in_word = false;
			}
			err = end_cmd(&st, op, &have_cmd);
			if (err)
				return err;
			s += c == ';' ? 1 : 2;
			continue;
		}
		if (!in_word) {
			struct sh_cmd *cmd = &p->cmd[p->n];

			if (cmd->argc >= SH_ARGV_MAX)
				return -E2BIG;
			cmd->argv[cmd->argc++] = p->store + st.used;
			in_word = true;
			have_cmd = true;
		}
		if (c == '\'') {
			s++;
			while (*s && *s != '\'') {
				err = emit(&st, *s++);
				if (err)
					return err;
			}
			if (!*s)
				return -EINVAL;
			s++;
		} else if (c == '"') {
			s++;
			while (*s && *s != '"') {
				if (*s == '\\' && (s[1] == '"' || s[1] == '\\' ||
						   s[1] == '$'))
					s++;
				if (*s == '$') {
					s++;
					err = expand(&st, &s);
				} else {
					err = emit(&st, *s++);
				}
				if (err)
					return err;
			}
			if (!*s)
				return -EINVAL;
			s++;
		} else if (c == '\\' && s[1]) {
			err = emit(&st, s[1]);
			s += 2;
			if (err)
				return err;
		} else if (c == '$') {
			s++;
			err = expand(&st, &s);
			if (err)
				return err;
		} else {
			err = emit(&st, c);
			s++;
			if (err)
				return err;
		}
	}
	if (in_word) {
		err = emit(&st, '\0');
		if (err)
			return err;
	}
	if (have_cmd || p->cmd[p->n].argc) {
		err = end_cmd(&st, SH_END, &have_cmd);
		if (err)
			return err;
	} else if (p->n && p->cmd[p->n - 1].op != SH_SEQ) {
		return -EINVAL;		/* "a &&" with nothing after it */
	}
	return 0;
}

/* ---- paths ----------------------------------------------------------- */

int sh_resolve(const char *cwd, const char *path, char *out, size_t size)
{
	char comb[160];
	size_t len = 1, i = 0;

	if (path[0] == '/')
		strlcpy(comb, path, sizeof(comb));
	else
		snprintf(comb, sizeof(comb), "%s/%s", cwd, path);
	if (size < 2)
		return -ENAMETOOLONG;
	out[0] = '/';
	out[1] = '\0';
	while (comb[i]) {
		char comp[64];
		size_t n = 0;

		while (comb[i] == '/')
			i++;
		while (comb[i] && comb[i] != '/') {
			if (n >= sizeof(comp) - 1)
				return -ENAMETOOLONG;
			comp[n++] = comb[i++];
		}
		comp[n] = '\0';
		if (!n || !strcmp(comp, "."))
			continue;
		if (!strcmp(comp, "..")) {
			while (len > 1 && out[len - 1] != '/')
				len--;
			if (len > 1)
				len--;		/* drop the slash before it */
			out[len] = '\0';
			continue;
		}
		if (len + n + 2 > size)
			return -ENAMETOOLONG;
		if (len > 1)
			out[len++] = '/';
		memcpy(out + len, comp, n + 1);
		len += n;
	}
	return 0;
}

/* ---- completion ------------------------------------------------------ */

int sh_match(const char *prefix, const char *const *names, int n,
	     char *common, size_t csize)
{
	size_t plen = strlen(prefix), clen = 0;
	int i, count = 0;

	common[0] = '\0';
	for (i = 0; i < n; i++) {
		if (strncmp(names[i], prefix, plen))
			continue;
		if (!count) {
			clen = strlcpy(common, names[i], csize);
			if (clen >= csize)
				clen = csize - 1;
		} else {
			size_t k = 0;

			while (k < clen && common[k] == names[i][k])
				k++;
			clen = k;
			common[k] = '\0';
		}
		count++;
	}
	return count;
}

/* ---- line editor ----------------------------------------------------- */

void se_init(struct sh_edit *e)
{
	memset(e, 0, sizeof(*e));
	e->browse = -1;
	e->limit = SH_LINE_MAX - 1;
}

void se_begin(struct sh_edit *e, int limit)
{
	e->len = e->cur = 0;
	e->buf[0] = '\0';
	e->browse = -1;
	e->limit = limit < SH_LINE_MAX - 1 ? limit : SH_LINE_MAX - 1;
	if (e->limit < 1)
		e->limit = 1;
}

void se_set(struct sh_edit *e, const char *text)
{
	e->len = (int)strlcpy(e->buf, text, (size_t)e->limit + 1);
	if (e->len > e->limit)
		e->len = e->limit;
	e->cur = e->len;
}

bool se_replace(struct sh_edit *e, int from, int to, const char *text)
{
	int tl = (int)strlen(text), tail = e->len - to;
	char rest[SH_LINE_MAX];

	if (from < 0 || to < from || to > e->len)
		return false;
	if (e->len - (to - from) + tl > e->limit)
		return false;
	memcpy(rest, e->buf + to, (size_t)tail);
	memcpy(e->buf + from, text, (size_t)tl);
	memcpy(e->buf + from + tl, rest, (size_t)tail);
	e->len = from + tl + tail;
	e->buf[e->len] = '\0';
	e->cur = from + tl;
	return true;
}

void se_history_add(struct sh_edit *e, const char *line)
{
	int i;

	if (!*line || (e->nhist && !strcmp(e->hist[0], line)))
		return;			/* empty, or a repeat of the last one */
	for (i = SH_HIST - 1; i > 0; i--)
		memcpy(e->hist[i], e->hist[i - 1], SH_LINE_MAX);
	strlcpy(e->hist[0], line, SH_LINE_MAX);
	if (e->nhist < SH_HIST)
		e->nhist++;
}

static void delete_range(struct sh_edit *e, int from, int to)
{
	memmove(e->buf + from, e->buf + to, (size_t)(e->len - to + 1));
	e->len -= to - from;
	e->cur = from;
}

static void browse_to(struct sh_edit *e, int idx)
{
	if (idx < 0) {
		se_set(e, e->saved);
		e->browse = -1;
		return;
	}
	if (e->browse < 0)
		memcpy(e->saved, e->buf, SH_LINE_MAX);
	e->browse = idx;
	se_set(e, e->hist[idx]);
}

int se_key(struct sh_edit *e, int key)
{
	int i;

	switch (key) {
	case '\n':
	case '\r':
		return SE_ENTER;
	case 3:
		return SE_ABORT;
	case 4:
		if (!e->len)
			return SE_EOF;
		if (e->cur < e->len)
			delete_range(e, e->cur, e->cur + 1);
		return SE_CONT;
	case '\t':
		return SE_TAB;
	case 12:
		return SE_CLEAR;
	case 1:
	case KEY_HOME:
		e->cur = 0;
		return SE_CONT;
	case 5:
	case KEY_END:
		e->cur = e->len;
		return SE_CONT;
	case KEY_LEFT:
		if (e->cur)
			e->cur--;
		return SE_CONT;
	case KEY_RIGHT:
		if (e->cur < e->len)
			e->cur++;
		return SE_CONT;
	case '\b':
	case 0x7F:
		if (e->cur)
			delete_range(e, e->cur - 1, e->cur);
		return SE_CONT;
	case KEY_DEL:
		if (e->cur < e->len)
			delete_range(e, e->cur, e->cur + 1);
		return SE_CONT;
	case 11:			/* Ctrl-K: kill to the end */
		e->len = e->cur;
		e->buf[e->len] = '\0';
		return SE_CONT;
	case 21:			/* Ctrl-U: kill to the start */
		delete_range(e, 0, e->cur);
		return SE_CONT;
	case 23:			/* Ctrl-W: kill the word before the cursor */
		i = e->cur;
		while (i > 0 && e->buf[i - 1] == ' ')
			i--;
		while (i > 0 && e->buf[i - 1] != ' ')
			i--;
		delete_range(e, i, e->cur);
		return SE_CONT;
	case KEY_UP:
		if (e->browse + 1 < e->nhist)
			browse_to(e, e->browse + 1);
		return SE_CONT;
	case KEY_DOWN:
		if (e->browse >= 0)
			browse_to(e, e->browse - 1);
		return SE_CONT;
	default:
		break;
	}
	if (key >= 0x20 && key < 0x7F && e->len < e->limit) {
		memmove(e->buf + e->cur + 1, e->buf + e->cur,
			(size_t)(e->len - e->cur + 1));
		e->buf[e->cur++] = (char)key;
		e->len++;
	}
	return SE_CONT;
}

size_t se_render(const struct sh_edit *e, const char *prompt, char *out,
		 size_t size)
{
	size_t n = 0;
	int back = e->len - e->cur;

	n += (size_t)snprintf(out + n, size - n, "\r%s%s\x1b[K", prompt, e->buf);
	if (back > 0 && n < size)
		n += (size_t)snprintf(out + n, size - n, "\x1b[%dD", back);
	return n;
}
