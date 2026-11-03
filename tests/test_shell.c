// SPDX-License-Identifier: GPL-2.0-only
/* Tests for the shell library: parser, variables, completion, line editor. */
#include "hosttest.h"
#include <string.h>
#include <hix/kernel.h>
#include <qubix/shlib.h>

static struct sh_vars vars;
static struct sh_parsed P;

static int parse(const char *line)
{
	return sh_parse(line, &P, &vars);
}

static void test_parser(void)
{
	printf("-- parser\n");
	sh_var_set(&vars, "NAME", "world");
	sh_var_set(&vars, "?", "3");

	CHECK(parse("echo hello   world") == 0);
	CHECK(P.n == 1 && P.cmd[0].argc == 3);
	CHECK_STR(P.cmd[0].argv[2], "world");
	CHECK(P.cmd[0].argv[3] == NULL);

	CHECK(parse("echo 'a  b' \"c  d\"") == 0);
	CHECK(P.cmd[0].argc == 3);
	CHECK_STR(P.cmd[0].argv[1], "a  b");
	CHECK_STR(P.cmd[0].argv[2], "c  d");

	CHECK(parse("echo $NAME ${NAME}x \"$NAME\" '$NAME' \\$NAME") == 0);
	CHECK_STR(P.cmd[0].argv[1], "world");
	CHECK_STR(P.cmd[0].argv[2], "worldx");
	CHECK_STR(P.cmd[0].argv[3], "world");
	CHECK_STR(P.cmd[0].argv[4], "$NAME");
	CHECK_STR(P.cmd[0].argv[5], "$NAME");

	CHECK(parse("echo $? $NOPE.") == 0);
	CHECK_STR(P.cmd[0].argv[1], "3");
	CHECK_STR(P.cmd[0].argv[2], ".");

	CHECK(parse("echo \"\" x") == 0);	/* an empty argument stays */
	CHECK(P.cmd[0].argc == 3);
	CHECK_STR(P.cmd[0].argv[1], "");

	CHECK(parse("a; b && c || d") == 0);
	CHECK(P.n == 4);
	CHECK(P.cmd[0].op == SH_SEQ && P.cmd[1].op == SH_AND);
	CHECK(P.cmd[2].op == SH_OR && P.cmd[3].op == SH_END);
	CHECK_STR(P.cmd[3].argv[0], "d");

	CHECK(parse("a;b&&c") == 0);		/* operators need no spaces */
	CHECK(P.n == 3);
	CHECK_STR(P.cmd[1].argv[0], "b");

	CHECK(parse("ls # a comment") == 0);
	CHECK(P.n == 1 && P.cmd[0].argc == 1);
	CHECK(parse("") == 0 && P.n == 0);
	CHECK(parse("   ") == 0 && P.n == 0);
	CHECK(parse("a;") == 0 && P.n == 1);	/* a trailing ; is fine */

	CHECK(parse("echo 'oops") == -EINVAL);
	CHECK(parse("echo \"oops") == -EINVAL);
	CHECK(parse("&& a") == -EINVAL);
	CHECK(parse("a &&") == -EINVAL);
	CHECK(parse("a && ; b") == -EINVAL);
	CHECK(parse("echo ${NAME") == -EINVAL);
}

static void test_limits(void)
{
	char line[600];
	int i;

	printf("-- parser limits\n");
	line[0] = '\0';
	for (i = 0; i < SH_ARGV_MAX + 2; i++)
		strlcpy(line + strlen(line), "x ", sizeof(line) - strlen(line));
	CHECK(parse(line) == -E2BIG);

	line[0] = '\0';
	for (i = 0; i < 100; i++)
		strlcpy(line + strlen(line), "word ", sizeof(line) - strlen(line));
	CHECK(parse("a") == 0);
	/* 100 words overflow the argv table before the store; either way: no crash */
	CHECK(parse(line) < 0);

	line[0] = '\0';
	for (i = 0; i < SH_CMDS_MAX + 2; i++)
		strlcpy(line + strlen(line), "a;", sizeof(line) - strlen(line));
	CHECK(parse(line) == -E2BIG);
}

static void test_vars(void)
{
	struct sh_vars v = { 0 };
	int i;
	char name[8];

	printf("-- variables\n");
	CHECK(sh_var_set(&v, "A", "1") == 0);
	CHECK_STR(sh_var_get(&v, "A"), "1");
	sh_var_set(&v, "A", "2");
	CHECK_STR(sh_var_get(&v, "A"), "2");
	CHECK(v.n == 1);
	CHECK(sh_var_get(&v, "B") == NULL);
	CHECK(sh_var_set(&v, "", "x") == -EINVAL);
	sh_var_set(&v, "B", "b");
	CHECK(sh_var_unset(&v, "A"));
	CHECK(sh_var_get(&v, "A") == NULL);
	CHECK_STR(sh_var_get(&v, "B"), "b");
	CHECK(!sh_var_unset(&v, "A"));
	for (i = 0; i < SH_VARS_MAX + 4; i++) {
		snprintf(name, sizeof(name), "v%d", i);
		if (sh_var_set(&v, name, "x") == -ENOSPC)
			break;
	}
	CHECK(v.n == SH_VARS_MAX);
}

static void test_paths(void)
{
	char o[32];

	printf("-- paths\n");
	CHECK(sh_resolve("/", "a", o, sizeof(o)) == 0);
	CHECK_STR(o, "/a");
	sh_resolve("/usr/bin", "../lib/x", o, sizeof(o));
	CHECK_STR(o, "/usr/lib/x");
	sh_resolve("/usr", "/etc//./hosts/", o, sizeof(o));
	CHECK_STR(o, "/etc/hosts");
	sh_resolve("/a/b", "../..", o, sizeof(o));
	CHECK_STR(o, "/");
	sh_resolve("/a", "../../..", o, sizeof(o));
	CHECK_STR(o, "/");
	sh_resolve("/a", ".", o, sizeof(o));
	CHECK_STR(o, "/a");
	sh_resolve("/", "", o, sizeof(o));
	CHECK_STR(o, "/");
	CHECK(sh_resolve("/aaaaaaaaaaaaaaaa", "bbbbbbbbbbbbbbbb", o, 20) ==
	      -ENAMETOOLONG);
}

static void test_match(void)
{
	static const char *const names[] = {
		"cat", "cd", "clear", "clock", "calc", "ls",
	};
	char common[16];

	printf("-- completion\n");
	CHECK(sh_match("c", names, 6, common, sizeof(common)) == 5);
	CHECK_STR(common, "c");
	CHECK(sh_match("cl", names, 6, common, sizeof(common)) == 2);
	CHECK_STR(common, "cl");
	CHECK(sh_match("cle", names, 6, common, sizeof(common)) == 1);
	CHECK_STR(common, "clear");
	CHECK(sh_match("clo", names, 6, common, sizeof(common)) == 1);
	CHECK(sh_match("z", names, 6, common, sizeof(common)) == 0);
	CHECK_STR(common, "");
	CHECK(sh_match("", names, 6, common, sizeof(common)) == 6);
	CHECK_STR(common, "");
}

static struct sh_edit E;

static void type(const char *s)
{
	while (*s)
		se_key(&E, *s++);
}

static void test_editor(void)
{
	char out[256];

	printf("-- line editor\n");
	se_init(&E);
	se_begin(&E, 40);
	type("helo");
	CHECK_STR(E.buf, "helo");
	se_key(&E, KEY_LEFT);
	se_key(&E, 'l');
	CHECK_STR(E.buf, "hello");
	CHECK(E.cur == 4);
	se_key(&E, KEY_HOME);
	se_key(&E, KEY_DEL);
	CHECK_STR(E.buf, "ello");
	se_key(&E, KEY_END);
	se_key(&E, '\b');
	CHECK_STR(E.buf, "ell");
	se_key(&E, 1);			/* Ctrl-A */
	type(">");
	CHECK_STR(E.buf, ">ell");
	se_key(&E, 5);			/* Ctrl-E */
	CHECK(E.cur == E.len);
	se_key(&E, 21);			/* Ctrl-U */
	CHECK_STR(E.buf, "");

	type("one two three");
	se_key(&E, 23);			/* Ctrl-W */
	CHECK_STR(E.buf, "one two ");
	se_key(&E, 23);
	CHECK_STR(E.buf, "one ");
	se_key(&E, KEY_HOME);
	se_key(&E, KEY_RIGHT);
	se_key(&E, 11);			/* Ctrl-K */
	CHECK_STR(E.buf, "o");

	CHECK(se_key(&E, '\n') == SE_ENTER);
	CHECK(se_key(&E, 3) == SE_ABORT);
	CHECK(se_key(&E, '\t') == SE_TAB);
	se_begin(&E, 40);
	CHECK(se_key(&E, 4) == SE_EOF);	/* Ctrl-D on an empty line */
	type("ab");
	se_key(&E, KEY_HOME);
	CHECK(se_key(&E, 4) == SE_CONT);	/* deletes under the cursor */
	CHECK_STR(E.buf, "b");

	/* the limit keeps a line on one screen row */
	se_begin(&E, 5);
	type("abcdefgh");
	CHECK_STR(E.buf, "abcde");
	CHECK(!se_replace(&E, 0, 0, "toolong"));
	CHECK(se_replace(&E, 1, 3, "XY"));
	CHECK_STR(E.buf, "aXYde");
	CHECK(E.cur == 3);

	/* control characters are never inserted */
	se_begin(&E, 20);
	se_key(&E, 7);
	se_key(&E, 0x1b);
	CHECK(E.len == 0);

	/* what the screen receives */
	se_begin(&E, 40);
	type("ls -l");
	se_key(&E, KEY_LEFT);
	se_key(&E, KEY_LEFT);
	se_render(&E, "# ", out, sizeof(out));
	CHECK_STR(out, "\r# ls -l\x1b[K\x1b[2D");
	se_key(&E, KEY_END);
	se_render(&E, "$ ", out, sizeof(out));
	CHECK_STR(out, "\r$ ls -l\x1b[K");
}

static void test_history(void)
{
	printf("-- history\n");
	se_init(&E);
	se_begin(&E, 60);
	se_history_add(&E, "first");
	se_history_add(&E, "second");
	se_history_add(&E, "second");		/* repeat: ignored */
	se_history_add(&E, "");		/* empty: ignored */
	CHECK(E.nhist == 2);

	type("draft");
	se_key(&E, KEY_UP);
	CHECK_STR(E.buf, "second");
	se_key(&E, KEY_UP);
	CHECK_STR(E.buf, "first");
	se_key(&E, KEY_UP);		/* the oldest stays */
	CHECK_STR(E.buf, "first");
	se_key(&E, KEY_DOWN);
	CHECK_STR(E.buf, "second");
	se_key(&E, KEY_DOWN);		/* back to what was being typed */
	CHECK_STR(E.buf, "draft");
	CHECK(E.browse == -1);
	se_key(&E, KEY_DOWN);
	CHECK_STR(E.buf, "draft");

	/* editing a recalled line works on a copy */
	se_key(&E, KEY_UP);
	type("!");
	CHECK_STR(E.buf, "second!");
	CHECK_STR(E.hist[0], "second");

	{
		int i;
		char l[16];

		for (i = 0; i < SH_HIST + 5; i++) {
			snprintf(l, sizeof(l), "cmd%d", i);
			se_history_add(&E, l);
		}
		CHECK(E.nhist == SH_HIST);
		CHECK_STR(E.hist[0], "cmd20");
	}
}

int main(void)
{
	printf("test_shell\n");
	test_parser();
	test_limits();
	test_vars();
	test_paths();
	test_match();
	test_editor();
	test_history();
	TEST_DONE();
}
