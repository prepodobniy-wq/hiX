// SPDX-License-Identifier: GPL-2.0-only
/*
 * desk: MS-Term, in spirit. A small command interpreter in a window:
 * dir, type, cd, ver, date, mem, ps, devtree, cls, run <program>, exit.
 * It reads the same kernel interfaces as the shell does, through libq.
 */
#include <hix/kernel.h>
#include <hix/version.h>
#include <qubix/libq.h>
#include <desk/desk.h>

#define LINE_W		76
#define MAX_LINES	70
#define PATH_MAX_LEN	60
#define IN_MAX		70

struct term {
	char lines[MAX_LINES][LINE_W + 1];
	int nlines;
	char cwd[PATH_MAX_LEN];
	char in[IN_MAX];
	int inlen;
	bool exit_after;
};

static void add_line(struct term *d, const char *s, int n)
{
	if (d->nlines == MAX_LINES) {
		memmove(d->lines[0], d->lines[1], sizeof(d->lines) - sizeof(d->lines[0]));
		d->nlines--;
	}
	n = min(n, LINE_W);
	memcpy(d->lines[d->nlines], s, (size_t)n);
	d->lines[d->nlines][n] = '\0';
	d->nlines++;
}

/* append text, splitting at newlines and at the window width */
static void put(struct term *d, const char *s)
{
	while (*s) {
		int n = 0;

		while (s[n] && s[n] != '\n' && n < LINE_W)
			n++;
		add_line(d, s, n);
		s += n;
		if (*s == '\n')
			s++;
	}
}

static void putf(struct term *d, const char *fmt, ...) __printf(2, 3);
static void putf(struct term *d, const char *fmt, ...)
{
	char buf[160];
	__builtin_va_list ap;

	__builtin_va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	__builtin_va_end(ap);
	put(d, buf);
}

/* make @path absolute and tidy (no ., .. or doubled slashes) */
static void resolve(const struct term *d, const char *path, char *out, size_t size)
{
	char tmp[PATH_MAX_LEN * 2];
	char *p, *seg;
	size_t len = 1;

	if (path[0] == '/')
		strlcpy(tmp, path, sizeof(tmp));
	else
		snprintf(tmp, sizeof(tmp), "%s/%s", d->cwd, path);
	out[0] = '/';
	out[1] = '\0';
	for (p = tmp; *p;) {
		size_t n = 0;

		while (*p == '/')
			p++;
		seg = p;
		while (p[n] && p[n] != '/')
			n++;
		p += n;
		if (!n || (n == 1 && seg[0] == '.'))
			continue;
		if (n == 2 && seg[0] == '.' && seg[1] == '.') {
			while (len > 1 && out[len - 1] != '/')
				len--;
			if (len > 1)
				len--;
			out[len] = '\0';
			continue;
		}
		if (len + n + 2 > size)
			break;
		if (len > 1)
			out[len++] = '/';
		memcpy(out + len, seg, n);
		len += n;
		out[len] = '\0';
	}
}

static void cmd_dir(struct term *d, const char *arg)
{
	struct dirent_rec recs[40];
	char path[PATH_MAX_LEN];
	long n, i;

	resolve(d, *arg ? arg : ".", path, sizeof(path));
	n = q_readdir(path, recs, 40);
	if (n < 0) {
		putf(d, "File not found - %s", path);
		return;
	}
	putf(d, " Directory of %s", path);
	put(d, "");
	for (i = 0; i < n; i++) {
		if (recs[i].type == VT_DIR)
			putf(d, "%-24s <DIR>", recs[i].name);
		else if (recs[i].type == VT_CHR)
			putf(d, "%-24s <DEV>", recs[i].name);
		else
			putf(d, "%-24s %10u", recs[i].name, recs[i].size);
	}
	putf(d, "        %ld entr%s", n, n == 1 ? "y" : "ies");
}

static void cmd_type(struct term *d, const char *arg)
{
	char path[PATH_MAX_LEN], buf[400];
	long fd, n;

	if (!*arg) {
		put(d, "Required parameter missing");
		return;
	}
	resolve(d, arg, path, sizeof(path));
	fd = q_open(path, O_RDONLY);
	if (fd < 0) {
		putf(d, "File not found - %s", arg);
		return;
	}
	while ((n = q_read((int)fd, buf, sizeof(buf) - 1)) > 0) {
		buf[n] = '\0';
		put(d, buf);
	}
	q_close((int)fd);
}

static void cmd_cd(struct term *d, const char *arg)
{
	struct dirent_rec rec[1];
	char path[PATH_MAX_LEN];

	if (!*arg) {
		put(d, d->cwd);
		return;
	}
	resolve(d, arg, path, sizeof(path));
	if (q_readdir(path, rec, 1) < 0)
		put(d, "Invalid directory");
	else
		strlcpy(d->cwd, path, sizeof(d->cwd));
}

static void cmd_sysctl(struct term *d, const char *name)
{
	char buf[1500];
	long n = q_sysctl(name, buf, sizeof(buf) - 1);

	if (n < 0)
		return;
	buf[n] = '\0';
	put(d, buf);
}

static void cmd_run(struct term *d, const char *arg)
{
	char name[24];
	const struct app *a;
	int n = 0;

	while (arg[n] && arg[n] != ' ' && n < (int)sizeof(name) - 1) {
		name[n] = arg[n];
		n++;
	}
	name[n] = '\0';
	a = n ? desk_find_app(name) : NULL;
	if (!a) {
		putf(d, "Bad command or file name - %s", name);
		return;
	}
	desk_launch(a, arg[n] ? arg + n + 1 : NULL);
}

static void execute(struct win *w, struct term *d, char *line)
{
	char *cmd = line, *arg;
	int i;

	while (*cmd == ' ')
		cmd++;
	arg = cmd;
	while (*arg && *arg != ' ')
		arg++;
	if (*arg)
		*arg++ = '\0';
	while (*arg == ' ')
		arg++;
	for (i = 0; cmd[i]; i++)
		if (cmd[i] >= 'A' && cmd[i] <= 'Z')
			cmd[i] += 32;
	if (!*cmd)
		return;

	if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
		put(d, "dir [path]   type <file>   cd [path]   cls   ver   date\n"
		       "mem   ps   devtree   uname   echo <text>   run <program>   exit\n"
		       "Programs: hub files notes calc clock sysinfo tasks\n"
		       "          mines reversi snake bricks");
	} else if (!strcmp(cmd, "dir") || !strcmp(cmd, "ls")) {
		cmd_dir(d, arg);
	} else if (!strcmp(cmd, "type") || !strcmp(cmd, "cat")) {
		cmd_type(d, arg);
	} else if (!strcmp(cmd, "cd") || !strcmp(cmd, "chdir")) {
		cmd_cd(d, arg);
	} else if (!strcmp(cmd, "cls") || !strcmp(cmd, "clear")) {
		d->nlines = 0;
	} else if (!strcmp(cmd, "ver") || !strcmp(cmd, "uname")) {
		cmd_sysctl(d, "kern.version");
	} else if (!strcmp(cmd, "date") || !strcmp(cmd, "time")) {
		long t = q_time(), s = t % 86400;

		putf(d, "Current time is %02ld:%02ld:%02ld", s / 3600,
		     s / 60 % 60, s % 60);
	} else if (!strcmp(cmd, "mem")) {
		cmd_sysctl(d, "hix.heap");
	} else if (!strcmp(cmd, "ps")) {
		cmd_sysctl(d, "hix.ps");
	} else if (!strcmp(cmd, "devtree")) {
		cmd_sysctl(d, "hix.devtree");
	} else if (!strcmp(cmd, "echo")) {
		put(d, arg);
	} else if (!strcmp(cmd, "run") || !strcmp(cmd, "start")) {
		cmd_run(d, arg);
	} else if (!strcmp(cmd, "exit")) {
		desk_close(w);
	} else {
		put(d, "Bad command or file name");
	}
}

static void dos_open(struct win *w, const char *arg)
{
	struct term *d = DESK_PRIV(w, struct term);

	(void)arg;
	strlcpy(d->cwd, "/", sizeof(d->cwd));
	put(d, "hix Command Prompt  [" HIX_NAME " " HIX_RELEASE "]\n"
	       "Type 'help' for the commands, 'exit' to close the window.\n");
}

static void dos_paint(struct win *w, struct steel_surface *c)
{
	struct term *d = DESK_PRIV(w, struct term);
	const u8 text = STEEL_ATTR(STEEL_LGRAY, STEEL_BLACK);
	char prompt[PATH_MAX_LEN + 4];
	int rows = c->h - 1, first = max(d->nlines - rows, 0), i, row = 0;

	steel_fill(c, ' ', text);
	for (i = first; i < d->nlines; i++, row++)
		steel_puts(c, 0, row, d->lines[i], text);
	snprintf(prompt, sizeof(prompt), "%s>", d->cwd);
	row = min(d->nlines - first, rows);
	steel_puts(c, 0, row, prompt, text);
	steel_puts(c, (int)strlen(prompt), row, d->in, text);
	steel_put(c, (int)strlen(prompt) + d->inlen, row, '_',
		  STEEL_ATTR(STEEL_WHITE, STEEL_BLACK));
}

static bool dos_key(struct win *w, int key, u8 mods)
{
	struct term *d = DESK_PRIV(w, struct term);

	(void)mods;
	if (key == '\n') {
		char line[IN_MAX];

		putf(d, "%s>%s", d->cwd, d->in);
		strlcpy(line, d->in, sizeof(line));
		d->in[0] = '\0';
		d->inlen = 0;
		execute(w, d, line);
		return true;
	}
	if ((key == 8 || key == 127) && d->inlen > 0) {
		d->in[--d->inlen] = '\0';
		return true;
	}
	if (key >= 0x20 && key < 0x7f && d->inlen < IN_MAX - 1) {
		d->in[d->inlen++] = (char)key;
		d->in[d->inlen] = '\0';
		return true;
	}
	return false;
}

const struct app app_term = {
	.name = "term",
	.title = "Term",
	.icon = { "\xDA\xC4\xC4\xC4\xC4\xBF", "\xB3" "C:\\_\xB3" },
	.icon_attr = { STEEL_ATTR(STEEL_LGRAY, STEEL_BLACK),
		       STEEL_ATTR(STEEL_LGRAY, STEEL_BLACK) },
	.cw = 76, .ch = 21,
	.flags = APP_SINGLE | APP_RESIZABLE,
	.open = dos_open,
	.paint = dos_paint,
	.key = dos_key,
};
