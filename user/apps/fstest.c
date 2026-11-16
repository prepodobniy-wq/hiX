// SPDX-License-Identifier: GPL-2.0-only
/* fstest: create, write, read back, delete a file; spawn a child and wait. */
#include "../sdk/hix.h"

int main(int argc, char **argv)
{
	char buf[64];
	int status = -1;
	long pid, n;

	(void)argc;
	(void)argv;
	if (hx_write_file("/tmp/fstest.txt", "written from ring 3\n", 20) != 20) {
		hx_puts("fstest: write failed\n");
		return 1;
	}
	n = hx_read_file("/tmp/fstest.txt", buf, sizeof(buf));
	hx_printf("fstest: read %d bytes: %s", (int)n, buf);
	hx_unlink("/tmp/fstest.txt");
	if (hx_open("/tmp/fstest.txt", HX_O_RDONLY) >= 0) {
		hx_puts("fstest: file still there\n");
		return 1;
	}
	pid = hx_spawn("primes");
	hx_wait(pid, &status);
	hx_printf("fstest: child %d exited with %d\n", (int)pid, status);
	hx_sleep_ms(100);
	hx_printf("fstest: time %d\n", (int)hx_time());
	return status;
}
