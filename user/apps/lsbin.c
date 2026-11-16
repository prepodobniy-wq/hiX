// SPDX-License-Identifier: GPL-2.0-only
/*
 * lsbin [dir]: list a directory from ring 3 (argv and getdirentries), then
 * show the first bytes of /etc/motd-like text when a file name is given.
 */
#include "../sdk/hix.h"

int main(int argc, char **argv)
{
	struct hx_dirent ents[32];
	const char *dir = argc > 1 ? argv[1] : "/bin";
	long n, i;

	hx_printf("lsbin: argc=%d, listing %s\n", argc, dir);
	n = hx_readdir(dir, ents, 32);
	if (n < 0) {
		hx_printf("lsbin: %s: error %d\n", dir, (int)n);
		return 1;
	}
	for (i = 0; i < n; i++)
		hx_printf("  %-12s %6u bytes %s\n", ents[i].name, ents[i].size,
			  ents[i].type == 1 ? "<dir>" : "");
	return 0;
}
