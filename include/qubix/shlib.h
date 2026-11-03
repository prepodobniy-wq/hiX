/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * qubix: the shell's reusable parts, free of system calls so the host tests
 * can run them: the command line parser, shell variables, name matching
 * for Tab completion, and the line editor.
 */
#ifndef _QUBIX_SHLIB_H
#define _QUBIX_SHLIB_H

#include <hix/types.h>
#include <qubix/input.h>

#define SH_LINE_MAX	128
#define SH_ARGV_MAX	16
#define SH_CMDS_MAX	8
#define SH_VARS_MAX	16
#define SH_HIST		16

/* ---- variables ------------------------------------------------------- */

struct sh_vars {
	int n;
	char name[SH_VARS_MAX][16];
	char val[SH_VARS_MAX][48];
};

int sh_var_set(struct sh_vars *v, const char *name, const char *val);
const char *sh_var_get(const struct sh_vars *v, const char *name);
bool sh_var_unset(struct sh_vars *v, const char *name);

/* ---- parser ---------------------------------------------------------- */

/* how a command is joined to the one after it */
enum { SH_END, SH_SEQ, SH_AND, SH_OR };	/* end, ';', '&&', '||' */

struct sh_cmd {
	char *argv[SH_ARGV_MAX + 1];
	int argc;
	int op;
};

struct sh_parsed {
	struct sh_cmd cmd[SH_CMDS_MAX];
	int n;
	char store[256];		/* the expanded words live here */
};

/*
 * Split @line into commands and words. Understands 'single' and "double"
 * quotes, backslash, $NAME ${NAME} $? expansion (not inside single quotes),
 * '#' comments, and the separators ; && ||. Returns 0, or -EINVAL for an
 * unterminated quote or a dangling operator, -E2BIG when it does not fit.
 */
int sh_parse(const char *line, struct sh_parsed *p, const struct sh_vars *v);

/* ---- paths ----------------------------------------------------------- */

/* make @path absolute against @cwd and fold "." ".." and "//";
 * -ENAMETOOLONG if it does not fit */
int sh_resolve(const char *cwd, const char *path, char *out, size_t size);

/* ---- completion ------------------------------------------------------ */

/* the names that start with @prefix; @common gets their longest common
 * prefix; returns how many matched */
int sh_match(const char *prefix, const char *const *names, int n,
	     char *common, size_t csize);

/* ---- line editor ----------------------------------------------------- */

struct sh_edit {
	char buf[SH_LINE_MAX];
	int len, cur;
	int limit;			/* longest line that fits on one row */
	char hist[SH_HIST][SH_LINE_MAX];
	int nhist;			/* stored lines, hist[0] is the newest */
	int browse;			/* -1 editing, else index in hist[] */
	char saved[SH_LINE_MAX];	/* the line being typed while browsing */
};

enum { SE_CONT, SE_ENTER, SE_EOF, SE_ABORT, SE_TAB, SE_CLEAR };

void se_init(struct sh_edit *e);
void se_begin(struct sh_edit *e, int limit);	/* start a fresh line */
int se_key(struct sh_edit *e, int key);		/* feed one key */
void se_set(struct sh_edit *e, const char *text);
/* replace buf[from, to) with @text (used by completion); false if too long */
bool se_replace(struct sh_edit *e, int from, int to, const char *text);
void se_history_add(struct sh_edit *e, const char *line);
/* the bytes that redraw the line: CR, prompt, text, erase, cursor back */
size_t se_render(const struct sh_edit *e, const char *prompt, char *out,
		 size_t size);

#endif /* _QUBIX_SHLIB_H */
