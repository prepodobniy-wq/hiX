// SPDX-License-Identifier: GPL-2.0-only
/*
 * qubix: the hix shell. A normal process: it only uses libq (int 0x80), the
 * same way a program on a real disk would.
 *
 * It reads the keyboard in raw mode and edits the line itself (history, cursor
 * keys, Tab completion; see shlib.c), understands quotes, $variables, `;`
 * `&&` `||`, keeps a working directory, and runs built-in commands from one
 * table. Programs from the qubix program table are started with `run`.
 */
#include <hix/kernel.h>
#include <qubix/libq.h>
#include <qubix/proc.h>
#include <qubix/shlib.h>
#include <qubix/tty.h>

#define COLS		80
#define PATH_MAX_LEN	96
#define DIR_MAX_ENTRIES	64

#define C_RESET		"\x1b[0m"
#define C_BOLD		"\x1b[1;37m"
#define C_GREEN		"\x1b[1;32m"
#define C_CYAN		"\x1b[1;36m"
#define C_RED		"\x1b[1;31m"
#define C_DIM		"\x1b[0;37m"

static struct sh_vars vars;
static struct sh_edit edit;
static struct sh_parsed parsed;
static char cwd[PATH_MAX_LEN] = "/";
static int last_status;
static bool want_exit;

/* ---- small helpers --------------------------------------------------- */

static void print_sysctl(const char *name)
{
	static char buf[2048];
	long n = q_sysctl(name, buf, sizeof(buf));

	if (n < 0)
		q_printf("sysctl: %s: error %ld\n", name, n);
	else
		q_write(1, buf, (size_t)n);
}

static unsigned long sysctl_num(const char *name)
{
	char buf[32] = "0";

	q_sysctl(name, buf, sizeof(buf));
	return simple_strtoul(buf, NULL, 10);
}

/* @path against the working directory; reports an error and returns false */
static bool full_path(const char *cmd, const char *path, char *out)
{
	if (sh_resolve(cwd, path, out, PATH_MAX_LEN)) {
		q_printf("%s: %s: path too long\n", cmd, path);
		return false;
	}
	return true;
}

static bool is_dir(const char *abs)
{
	struct dirent_rec rec;

	return q_readdir(abs, &rec, 1) >= 0;
}

static void banner(void)
{
	q_puts(C_CYAN
	       "  _     _      \n"
	       " | |__ (_)_  __\n"
	       " | '_ \\| \\ \\/ /   " C_BOLD "hix" C_CYAN " - hi is not unix\n"
	       " | | | | |>  <    " C_RESET "aether " C_CYAN "+" C_RESET " qubix "
	       C_CYAN "+" C_RESET " steel\n"
	       C_CYAN " |_| |_|_/_/\\_\\\n" C_RESET "\n");
	print_sysctl("kern.version");
	q_puts("\nType 'help' for the command list; Tab completes, Up/Down "
	       "recall.\n\n");
}

/* ---- file commands --------------------------------------------------- */

static struct dirent_rec recs[DIR_MAX_ENTRIES];

static int cmp_recs(const struct dirent_rec *a, const struct dirent_rec *b)
{
	/* directories first, then by name */
	if ((a->type == VT_DIR) != (b->type == VT_DIR))
		return a->type == VT_DIR ? -1 : 1;
	return strcmp(a->name, b->name);
}

static void sort_recs(long n)
{
	long i, j;
	struct dirent_rec tmp;

	for (i = 1; i < n; i++) {
		tmp = recs[i];
		for (j = i; j > 0 && cmp_recs(&tmp, &recs[j - 1]) < 0; j--)
			recs[j] = recs[j - 1];
		recs[j] = tmp;
	}
}

static int cmd_ls(int argc, char **argv)
{
	bool lng = false;
	const char *target = NULL;
	char abs[PATH_MAX_LEN];
	long n, i;
	int a, col = 0;

	for (a = 1; a < argc; a++) {
		if (!strcmp(argv[a], "-l"))
			lng = true;
		else
			target = argv[a];
	}
	if (!full_path("ls", target ? target : ".", abs))
		return 1;
	n = q_readdir(abs, recs, DIR_MAX_ENTRIES);
	if (n < 0) {
		q_printf("ls: %s: error %ld\n", abs, n);
		return 1;
	}
	sort_recs(n);
	for (i = 0; i < n; i++) {
		const struct dirent_rec *r = &recs[i];

		if (lng) {
			if (r->type == VT_DIR)
				q_printf("d %8s  " C_CYAN "%s/" C_RESET "\n", "-",
					 r->name);
			else if (r->type == VT_CHR)
				q_printf("c %8s  " C_GREEN "%s" C_RESET "\n", "-",
					 r->name);
			else
				q_printf("- %8u  %s\n", r->size, r->name);
			continue;
		}
		/* columns of 20 */
		{
			const char *color = r->type == VT_DIR ? C_CYAN :
					    r->type == VT_CHR ? C_GREEN : "";
			int len = (int)strlen(r->name) + (r->type == VT_DIR);

			q_printf("%s%s%s" C_RESET "%*s", color, r->name,
				 r->type == VT_DIR ? "/" : "", len < 20 ? 20 - len : 1,
				 "");
		}
		if (++col == COLS / 20) {
			q_puts("\n");
			col = 0;
		}
	}
	if (col)
		q_puts("\n");
	return 0;
}

static int cmd_cd(int argc, char **argv)
{
	char abs[PATH_MAX_LEN];

	if (!full_path("cd", argc > 1 ? argv[1] : "/", abs))
		return 1;
	if (!is_dir(abs)) {
		q_printf("cd: %s: not a directory\n", abs);
		return 1;
	}
	strlcpy(cwd, abs, sizeof(cwd));
	sh_var_set(&vars, "PWD", cwd);
	return 0;
}

static int cmd_pwd(int argc, char **argv)
{
	q_printf("%s\n", cwd);
	return 0;
}

static int cat_fd(long fd)
{
	char buf[256];
	long n;

	while ((n = q_read((int)fd, buf, sizeof(buf))) > 0)
		q_write(1, buf, (size_t)n);
	return 0;
}

static int cmd_cat(int argc, char **argv)
{
	int a, rc = 0;

	if (argc < 2) {
		q_puts("usage: cat <file>...\n");
		return 1;
	}
	for (a = 1; a < argc; a++) {
		char abs[PATH_MAX_LEN];
		long fd;

		if (!full_path("cat", argv[a], abs))
			return 1;
		fd = q_open(abs, O_RDONLY);
		if (fd < 0) {
			q_printf("cat: %s: error %ld\n", argv[a], fd);
			rc = 1;
			continue;
		}
		cat_fd(fd);
		q_close((int)fd);
	}
	return rc;
}

static int cmd_echo(int argc, char **argv)
{
	int i, end = argc;
	bool append = false, redirect = false;
	long fd = 1;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], ">") || !strcmp(argv[i], ">>")) {
			append = argv[i][1] == '>';
			redirect = true;
			end = i;
			break;
		}
	}
	if (redirect) {
		char abs[PATH_MAX_LEN];

		if (end + 1 >= argc) {
			q_puts("echo: missing file name\n");
			return 1;
		}
		if (!full_path("echo", argv[end + 1], abs))
			return 1;
		fd = q_open(abs, O_WRONLY | O_CREAT |
			    (append ? O_APPEND : O_TRUNC));
		if (fd < 0) {
			q_printf("echo: %s: error %ld\n", argv[end + 1], fd);
			return 1;
		}
	}
	for (i = 1; i < end; i++) {
		q_write((int)fd, argv[i], strlen(argv[i]));
		q_write((int)fd, i + 1 < end ? " " : "\n", 1);
	}
	if (end == 1)
		q_write((int)fd, "\n", 1);
	if (fd != 1)
		q_close((int)fd);
	return 0;
}

static int cmd_mkdir(int argc, char **argv)
{
	char abs[PATH_MAX_LEN];
	long r;

	if (argc < 2) {
		q_puts("usage: mkdir <dir>\n");
		return 1;
	}
	if (!full_path("mkdir", argv[1], abs))
		return 1;
	r = q_mkdir(abs);
	if (r)
		q_printf("mkdir: %s: error %ld\n", argv[1], r);
	return r ? 1 : 0;
}

static int cmd_rm(int argc, char **argv)
{
	char abs[PATH_MAX_LEN];
	long r;

	if (argc < 2) {
		q_puts("usage: rm <path>\n");
		return 1;
	}
	if (!full_path("rm", argv[1], abs))
		return 1;
	r = q_unlink(abs);
	if (r)
		q_printf("rm: %s: error %ld\n", argv[1], r);
	return r ? 1 : 0;
}

static int cmd_touch(int argc, char **argv)
{
	char abs[PATH_MAX_LEN];
	long fd;

	if (argc < 2) {
		q_puts("usage: touch <file>\n");
		return 1;
	}
	if (!full_path("touch", argv[1], abs))
		return 1;
	fd = q_open(abs, O_WRONLY | O_CREAT | O_APPEND);
	if (fd < 0) {
		q_printf("touch: %s: error %ld\n", argv[1], fd);
		return 1;
	}
	q_close((int)fd);
	return 0;
}

static int copy_file(const char *cmd, const char *from, const char *to)
{
	char a[PATH_MAX_LEN], b[PATH_MAX_LEN], buf[256];
	long in, out, n;

	if (!full_path(cmd, from, a) || !full_path(cmd, to, b))
		return 1;
	in = q_open(a, O_RDONLY);
	if (in < 0) {
		q_printf("%s: %s: error %ld\n", cmd, from, in);
		return 1;
	}
	if (is_dir(b)) {
		/* copying into a directory keeps the file name */
		const char *base = strrchr(a, '/');
		size_t l = strlen(b);

		snprintf(b + l, sizeof(b) - l, "%s%s", l > 1 ? "/" : "",
			 base ? base + 1 : a);
	}
	out = q_open(b, O_WRONLY | O_CREAT | O_TRUNC);
	if (out < 0) {
		q_printf("%s: %s: error %ld\n", cmd, to, out);
		q_close((int)in);
		return 1;
	}
	while ((n = q_read((int)in, buf, sizeof(buf))) > 0)
		q_write((int)out, buf, (size_t)n);
	q_close((int)in);
	q_close((int)out);
	return 0;
}

static int cmd_cp(int argc, char **argv)
{
	if (argc < 3) {
		q_puts("usage: cp <from> <to>\n");
		return 1;
	}
	return copy_file("cp", argv[1], argv[2]);
}

static int cmd_mv(int argc, char **argv)
{
	char abs[PATH_MAX_LEN];

	if (argc < 3) {
		q_puts("usage: mv <from> <to>\n");
		return 1;
	}
	if (copy_file("mv", argv[1], argv[2]))
		return 1;
	if (!full_path("mv", argv[1], abs))
		return 1;
	return q_unlink(abs) ? 1 : 0;
}

/* wc, head and grep read a file whole: files in the memory VFS are small */
static long slurp(const char *cmd, const char *path, char *buf, size_t size)
{
	char abs[PATH_MAX_LEN];
	long fd, n, total = 0;

	if (!full_path(cmd, path, abs))
		return -1;
	fd = q_open(abs, O_RDONLY);
	if (fd < 0) {
		q_printf("%s: %s: error %ld\n", cmd, path, fd);
		return -1;
	}
	while (total < (long)size - 1 &&
	       (n = q_read((int)fd, buf + total, size - 1 - total)) > 0)
		total += n;
	q_close((int)fd);
	buf[total] = '\0';
	return total;
}

static char filebuf[8192];

static int cmd_wc(int argc, char **argv)
{
	long n, lines = 0, words = 0, i;
	bool in_word = false;

	if (argc < 2) {
		q_puts("usage: wc <file>\n");
		return 1;
	}
	n = slurp("wc", argv[1], filebuf, sizeof(filebuf));
	if (n < 0)
		return 1;
	for (i = 0; i < n; i++) {
		char c = filebuf[i];

		if (c == '\n')
			lines++;
		if (c == ' ' || c == '\n' || c == '\t') {
			in_word = false;
		} else if (!in_word) {
			in_word = true;
			words++;
		}
	}
	q_printf("%ld %ld %ld %s\n", lines, words, n, argv[1]);
	return 0;
}

static int cmd_head(int argc, char **argv)
{
	long want = 10, n, seen = 0, i;
	const char *file = NULL;
	int a;

	for (a = 1; a < argc; a++) {
		if (!strcmp(argv[a], "-n") && a + 1 < argc)
			want = (long)simple_strtoul(argv[++a], NULL, 10);
		else
			file = argv[a];
	}
	if (!file) {
		q_puts("usage: head [-n lines] <file>\n");
		return 1;
	}
	n = slurp("head", file, filebuf, sizeof(filebuf));
	if (n < 0)
		return 1;
	for (i = 0; i < n && seen < want; i++) {
		q_write(1, &filebuf[i], 1);
		if (filebuf[i] == '\n')
			seen++;
	}
	return 0;
}

static int cmd_grep(int argc, char **argv)
{
	char *p, *line;
	long n;
	int found = 0;

	if (argc < 3) {
		q_puts("usage: grep <text> <file>\n");
		return 1;
	}
	n = slurp("grep", argv[2], filebuf, sizeof(filebuf));
	if (n < 0)
		return 1;
	for (line = filebuf; *line; line = p ? p + 1 : line + strlen(line)) {
		p = strchr(line, '\n');
		if (p)
			*p = '\0';
		if (strstr(line, argv[1])) {
			q_printf("%s\n", line);
			found++;
		}
		if (!p)
			break;
	}
	return found ? 0 : 1;
}

/* ---- system commands ------------------------------------------------- */

static int cmd_uname(int argc, char **argv)
{
	print_sysctl(argc > 1 ? "kern.version" : "kern.ostype");
	q_puts("\n");
	return 0;
}

static int cmd_uptime(int argc, char **argv)
{
	unsigned long s = sysctl_num("kern.uptime");

	q_printf("up %lu:%02lu:%02lu\n", s / 3600, s % 3600 / 60, s % 60);
	return 0;
}

static int cmd_date(int argc, char **argv)
{
	long t = q_time(), days, secs;
	long y, m, d;
	long z, era, doe, yoe, doy, mp;

	if (t < 0) {
		q_puts("date: no clock\n");
		return 1;
	}
	days = t / 86400;
	secs = t % 86400;
	/* civil-from-days */
	z = days + 719468;
	era = z / 146097;
	doe = z - era * 146097;
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	y = yoe + era * 400;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	d = doy - (153 * mp + 2) / 5 + 1;
	m = mp < 10 ? mp + 3 : mp - 9;
	y += m <= 2;
	q_printf("%04ld-%02ld-%02ld %02ld:%02ld:%02ld UTC\n", y, m, d,
		 secs / 3600, secs % 3600 / 60, secs % 60);
	return 0;
}

static int cmd_mem(int argc, char **argv)
{
	unsigned long total = sysctl_num("hw.memsize") / 1024;
	unsigned long freek = sysctl_num("hw.freemem") / 1024;
	unsigned long used = total - freek;
	int bar = total ? (int)(used * 30 / total) : 0, i;

	q_printf("RAM managed: %lu KiB\nRAM free:    %lu KiB\nRAM used:    %lu KiB"
		 "  [", total, freek, used);
	for (i = 0; i < 30; i++)
		q_puts(i < bar ? "#" : ".");
	q_puts("]\n");
	print_sysctl("hix.heap");
	return 0;
}

static int cmd_ps(int argc, char **argv)
{
	print_sysctl("hix.procs");
	q_puts("\n");
	print_sysctl("hix.ps");
	return 0;
}

/* a live view of ps and memory; any key leaves */
static int cmd_top(int argc, char **argv)
{
	int key = 0;
	long n;

	q_ioctl(0, TTY_SETRAW, 1);
	q_ioctl(0, TTY_SETTIMEOUT, 1000);
	for (;;) {
		q_puts("\x1b[2J");
		cmd_uptime(0, NULL);
		cmd_mem(0, NULL);
		q_puts("\n");
		cmd_ps(0, NULL);
		q_puts("\n" C_DIM "(any key leaves top)" C_RESET);
		n = q_read(0, &key, sizeof(key));
		if (n > 0)
			break;
	}
	q_ioctl(0, TTY_SETRAW, 0);
	q_puts("\x1b[2J");
	return 0;
}

static int cmd_devtree(int argc, char **argv)
{
	print_sysctl("hix.devtree");
	return 0;
}

static int cmd_irqs(int argc, char **argv)
{
	print_sysctl("hix.irqs");
	return 0;
}

static int cmd_dmesg(int argc, char **argv)
{
	static char log[16384];

	q_sysctl("hix.dmesg", log, sizeof(log));
	q_puts(log);
	return 0;
}

static int cmd_sysctl(int argc, char **argv)
{
	if (argc < 2) {
		q_puts("usage: sysctl <kern.ostype|kern.version|hw.memsize|"
		       "hix.ps|hix.heap|...>\n");
		return 1;
	}
	print_sysctl(argv[1]);
	q_puts("\n");
	return 0;
}

static int cmd_progs(int argc, char **argv)
{
	int i;

	for (i = 0; i < qprogs_count; i++)
		q_printf("  %-10s %s\n", qprogs[i].name, qprogs[i].help);
	return 0;
}

static long spawn_and_wait(const char *cmd, int *status)
{
	long pid = q_spawn(cmd);

	*status = 0;
	if (pid < 0)
		return pid;
	q_wait4(pid, status);
	return pid;
}

static int cmd_run(int argc, char **argv)
{
	char cmd[SH_LINE_MAX] = "";
	int i, status;
	long pid;

	if (argc < 2) {
		q_puts("usage: run <program> [args]   ('progs' lists them)\n");
		return 1;
	}
	for (i = 1; i < argc; i++) {
		strlcpy(cmd + strlen(cmd), argv[i], sizeof(cmd) - strlen(cmd));
		if (i + 1 < argc)
			strlcpy(cmd + strlen(cmd), " ", sizeof(cmd) - strlen(cmd));
	}
	pid = spawn_and_wait(cmd, &status);
	if (pid < 0) {
		q_printf("run: %s: error %ld\n", cmd, pid);
		return 1;
	}
	if (status)
		q_printf("[pid %ld exited with %d]\n", pid, status);
	return status;
}

static int cmd_ipc(int argc, char **argv)
{
	int st;
	long srv = q_spawn("ipcsrv"), cli;

	if (srv < 0) {
		q_printf("ipc: %ld\n", srv);
		return 1;
	}
	cli = q_spawn("ipccli");
	if (cli >= 0)
		q_wait4(cli, &st);
	q_wait4(srv, &st);
	return 0;
}

static int run_desk(const char *program)
{
	char cmd[48] = "desk ";
	int status;
	long pid;

	strlcpy(cmd + 5, program, sizeof(cmd) - 5);
	pid = spawn_and_wait(cmd, &status);
	if (pid < 0) {
		q_printf("desk: error %ld\n", pid);
		return 1;
	}
	if (status)
		q_printf("[desk exited with %d]\n", status);
	return status;
}

static int cmd_desk(int argc, char **argv)
{
	return run_desk(argc > 1 ? argv[1] : "");
}

static int cmd_gfx(int argc, char **argv)
{
	q_steelctl(STEEL_CTL_DEMO, 0, 0, 0);
	return 0;
}

static int cmd_clear(int argc, char **argv)
{
	q_puts("\x1b[2J");
	return 0;
}

static int cmd_sleep(int argc, char **argv)
{
	q_sleep_ms((argc > 1 ? simple_strtoul(argv[1], NULL, 10) : 1) * 1000);
	return 0;
}

static int cmd_crash(int argc, char **argv)
{
	q_puts("writing to address 0...\n");
	*(volatile int *)0 = 1;
	return 0;
}

static int cmd_panic(int argc, char **argv)
{
	q_reboot(RB_PANIC);
	return 0;
}

static int cmd_reboot(int argc, char **argv)
{
	q_reboot(RB_AUTOBOOT);
	return 0;
}

static int cmd_halt(int argc, char **argv)
{
	q_reboot(RB_HALT);
	return 0;
}

static int cmd_exit(int argc, char **argv)
{
	want_exit = true;
	return 0;
}

static int cmd_true(int argc, char **argv)
{
	return 0;
}

static int cmd_false(int argc, char **argv)
{
	return 1;
}

/* ---- shell state commands -------------------------------------------- */

static int cmd_set(int argc, char **argv)
{
	int i;

	if (argc == 1) {
		for (i = 0; i < vars.n; i++)
			q_printf("%s=%s\n", vars.name[i], vars.val[i]);
		return 0;
	}
	for (i = 1; i < argc; i++) {
		char *eq = strchr(argv[i], '=');

		if (!eq || eq == argv[i]) {
			q_puts("usage: set NAME=value ...\n");
			return 1;
		}
		*eq = '\0';
		if (sh_var_set(&vars, argv[i], eq + 1)) {
			q_printf("set: cannot set %s\n", argv[i]);
			return 1;
		}
	}
	return 0;
}

static int cmd_unset(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++)
		sh_var_unset(&vars, argv[i]);
	return 0;
}

static int cmd_history(int argc, char **argv)
{
	int i;

	for (i = edit.nhist - 1; i >= 0; i--)
		q_printf("%3d  %s\n", edit.nhist - i, edit.hist[i]);
	return 0;
}

static int cmd_help(int argc, char **argv);

/* ---- the command table ----------------------------------------------- */

struct builtin {
	const char *name;
	int (*fn)(int argc, char **argv);
	const char *help;
};

static const struct builtin builtins[] = {
	{ "help", cmd_help, "this list" },
	{ "uname", cmd_uname, "uname [-a]: system name" },
	{ "uptime", cmd_uptime, "time since boot" },
	{ "date", cmd_date, "date and time (UTC)" },
	{ "mem", cmd_mem, "memory and heap statistics" },
	{ "free", cmd_mem, "same as mem" },
	{ "ps", cmd_ps, "tasks, threads and qubix processes" },
	{ "top", cmd_top, "live ps and memory; any key leaves" },
	{ "devtree", cmd_devtree, "tree of drivers" },
	{ "irqs", cmd_irqs, "interrupt counters" },
	{ "dmesg", cmd_dmesg, "kernel log (PgUp/PgDn scroll the screen)" },
	{ "sysctl", cmd_sysctl, "sysctl <name>: kern.* hw.* hix.* values" },
	{ "ls", cmd_ls, "ls [-l] [dir]: list a directory" },
	{ "cd", cmd_cd, "cd [dir]: change directory" },
	{ "pwd", cmd_pwd, "print the working directory" },
	{ "cat", cmd_cat, "cat <file>...: print files" },
	{ "echo", cmd_echo, "echo text [> file]: print or write (>> appends)" },
	{ "mkdir", cmd_mkdir, "mkdir <dir>" },
	{ "rm", cmd_rm, "rm <path>" },
	{ "touch", cmd_touch, "touch <file>: create an empty file" },
	{ "cp", cmd_cp, "cp <from> <to>" },
	{ "mv", cmd_mv, "mv <from> <to>" },
	{ "wc", cmd_wc, "wc <file>: lines, words, bytes" },
	{ "head", cmd_head, "head [-n lines] <file>" },
	{ "grep", cmd_grep, "grep <text> <file>" },
	{ "set", cmd_set, "set [NAME=value]: variables; use $NAME and $?" },
	{ "unset", cmd_unset, "unset NAME" },
	{ "history", cmd_history, "the command history" },
	{ "progs", cmd_progs, "list the programs" },
	{ "run", cmd_run, "run <prog> [args]: run a program and wait" },
	{ "ipc", cmd_ipc, "port IPC demo: client and server processes" },
	{ "desk", cmd_desk, "desk [program]: the desktop" },
	{ "gfx", cmd_gfx, "steel graphics demo" },
	{ "clear", cmd_clear, "clear the screen (Ctrl-L too)" },
	{ "sleep", cmd_sleep, "sleep <seconds>" },
	{ "true", cmd_true, "do nothing, successfully" },
	{ "false", cmd_false, "do nothing, unsuccessfully" },
	{ "crash", cmd_crash, "write to address 0 (shows the fault report)" },
	{ "panic", cmd_panic, "trigger a kernel panic" },
	{ "reboot", cmd_reboot, "restart" },
	{ "halt", cmd_halt, "stop the machine" },
	{ "exit", cmd_exit, "leave the shell (init starts a new one)" },
};

static int cmd_help(int argc, char **argv)
{
	size_t i;

	q_puts("commands:\n");
	for (i = 0; i < ARRAY_SIZE(builtins); i++)
		q_printf("  %-8s %s\n", builtins[i].name, builtins[i].help);
	q_puts("\nprograms on the desktop can be typed by name: hub files notes\n"
	       "calc clock sysinfo tasks term mines reversi snake bricks chess\n"
	       "\nediting: arrows, Home/End, Del, Ctrl-A/E/K/U/W, Up/Down history,\n"
	       "Tab completes commands and file names.\n"
	       "syntax: 'quotes', \"quotes with $VAR\", ; && ||, # comment.\n");
	return 0;
}

/* programs that live on the desktop can be typed like commands */
static const char * const desk_programs[] = {
	"hub", "files", "notes", "calc", "clock", "sysinfo", "tasks", "term",
	"mines", "reversi", "snake", "bricks", "chess",
};

static bool is_desk_program(const char *name)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(desk_programs); i++)
		if (!strcmp(desk_programs[i], name))
			return true;
	return false;
}

/* ---- running a line -------------------------------------------------- */

static int run_one(struct sh_cmd *c)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(builtins); i++)
		if (!strcmp(builtins[i].name, c->argv[0]))
			return builtins[i].fn(c->argc, c->argv);
	if (is_desk_program(c->argv[0]))
		return run_desk(c->argv[0]);
	q_printf(C_RED "sh: %s: command not found" C_RESET "\n", c->argv[0]);
	return 127;
}

static void set_status(int status)
{
	char num[12];

	last_status = status;
	snprintf(num, sizeof(num), "%d", status);
	sh_var_set(&vars, "?", num);
}

static void run_line(const char *line)
{
	int err, i;
	bool skip = false;

	sh_var_set(&vars, "?", last_status ? "1" : "0");
	err = sh_parse(line, &parsed, &vars);
	if (err) {
		q_printf(C_RED "sh: syntax error (%s)" C_RESET "\n",
			 err == -E2BIG ? "line too complex" : "check the quotes");
		set_status(2);
		return;
	}
	for (i = 0; i < parsed.n && !want_exit; i++) {
		struct sh_cmd *c = &parsed.cmd[i];

		if (!skip)
			set_status(run_one(c));
		/* && runs the next one after success, || after failure */
		if (c->op == SH_AND)
			skip = last_status != 0;
		else if (c->op == SH_OR)
			skip = last_status == 0;
		else
			skip = false;
	}
}

/* ---- reading a line -------------------------------------------------- */

static int prompt_text(char *out, size_t size)
{
	snprintf(out, size, "%shix" C_RESET ":" C_CYAN "%s" C_RESET "# ",
		 last_status ? C_RED : C_GREEN, cwd);
	/* visible width: "hix" ":" cwd "# " */
	return 3 + 1 + (int)strlen(cwd) + 2;
}

static void redraw(const char *prompt)
{
	char out[320];
	size_t n = se_render(&edit, prompt, out, sizeof(out));

	q_write(1, out, n);
}

/* Tab: complete the word under the cursor */
static void complete(const char *prompt)
{
	static const char *names[DIR_MAX_ENTRIES + 64];
	static char store[DIR_MAX_ENTRIES][VFS_NAME_MAX + 2];
	char common[VFS_NAME_MAX + 2], word[PATH_MAX_LEN], dirpart[PATH_MAX_LEN];
	int start = edit.cur, n = 0, count, i;
	bool first, path_mode;
	const char *base;
	char *slash;

	while (start > 0 && edit.buf[start - 1] != ' ')
		start--;
	if (edit.cur - start >= (int)sizeof(word))
		return;
	memcpy(word, edit.buf + start, (size_t)(edit.cur - start));
	word[edit.cur - start] = '\0';
	first = true;
	for (i = 0; i < start; i++)
		if (edit.buf[i] != ' ')
			first = false;
	path_mode = !first || strchr(word, '/');

	if (!path_mode) {
		size_t k;

		for (k = 0; k < ARRAY_SIZE(builtins) && n < 120; k++)
			names[n++] = builtins[k].name;
		for (k = 0; k < ARRAY_SIZE(desk_programs) && n < 120; k++)
			names[n++] = desk_programs[k];
		base = word;
		dirpart[0] = '\0';
	} else {
		char abs[PATH_MAX_LEN];
		long cnt;

		slash = strrchr(word, '/');
		if (slash) {
			size_t dl = (size_t)(slash - word) + 1;

			memcpy(dirpart, word, dl);
			dirpart[dl] = '\0';
			base = slash + 1;
		} else {
			dirpart[0] = '\0';
			base = word;
		}
		if (sh_resolve(cwd, dirpart[0] ? dirpart : ".", abs,
			       sizeof(abs)))
			return;
		cnt = q_readdir(abs, recs, DIR_MAX_ENTRIES);
		for (i = 0; i < cnt; i++) {
			snprintf(store[i], sizeof(store[i]), "%s%s", recs[i].name,
				 recs[i].type == VT_DIR ? "/" : "");
			names[n++] = store[i];
		}
	}

	count = sh_match(base, names, n, common, sizeof(common));
	if (!count) {
		q_puts("\a");
		return;
	}
	if (count == 1 || strlen(common) > strlen(base)) {
		char text[PATH_MAX_LEN];

		snprintf(text, sizeof(text), "%s%s%s", dirpart, common,
			 count == 1 && common[strlen(common) - 1] != '/' ? " " : "");
		se_replace(&edit, start, edit.cur, text);
		redraw(prompt);
		return;
	}
	/* several candidates and nothing to add: list them */
	q_puts("\n");
	for (i = 0; i < n; i++)
		if (!strncmp(names[i], base, strlen(base)))
			q_printf("%s  ", names[i]);
	q_puts("\n");
	redraw(prompt);
}

/* returns false at end of input */
static bool read_line(char *out, size_t size)
{
	char prompt[160];
	int width = prompt_text(prompt, sizeof(prompt));
	bool ok = true;

	se_begin(&edit, COLS - 1 - width);
	q_ioctl(0, TTY_SETTIMEOUT, 0);
	q_ioctl(0, TTY_SETRAW, 1);
	redraw(prompt);
	for (;;) {
		int key = 0, act;
		long n = q_read(0, &key, sizeof(key));

		if (n < (long)sizeof(key))
			continue;
		act = se_key(&edit, key);
		if (act == SE_ENTER) {
			q_puts("\n");
			break;
		}
		if (act == SE_EOF) {
			q_puts("\n");
			ok = false;
			break;
		}
		if (act == SE_ABORT) {
			q_puts("^C\n");
			edit.buf[0] = '\0';
			break;
		}
		if (act == SE_CLEAR)
			q_puts("\x1b[2J");
		else if (act == SE_TAB)
			complete(prompt);
		else
			redraw(prompt);
	}
	q_ioctl(0, TTY_SETRAW, 0);
	strlcpy(out, edit.buf, size);
	return ok;
}

int sh_main(int argc, char **argv)
{
	static char line[SH_LINE_MAX];

	(void)argc;
	(void)argv;
	se_init(&edit);
	sh_var_set(&vars, "PWD", cwd);
	sh_var_set(&vars, "HOSTNAME", "hix");
	sh_var_set(&vars, "?", "0");
	banner();

	while (!want_exit) {
		if (!read_line(line, sizeof(line)))
			break;
		se_history_add(&edit, line);
		run_line(line);
	}
	return 0;
}
