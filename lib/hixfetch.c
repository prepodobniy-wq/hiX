// SPDX-License-Identifier: GPL-2.0-only
/*
 * hixfetch: the system summary shown by the shell's and Term's `hixfetch`
 * command, in the spirit of screenfetch: a logo on the left, facts on the
 * right. Everything it prints is read through sysctl.
 */
#include <hix/kernel.h>
#include <qubix/libq.h>
#include <qubix/hixfetch.h>

#define LOGO_W		22

static const char *const logo[] = {
	"   _     _            ",
	"  | |__ (_)__  __     ",
	"  | '_ \\| |\\ \\/ /     ",
	"  | | | | | >  <      ",
	"  |_| |_|_|/_/\\_\\     ",
	"  hi is not unix      ",
};

static void get(const char *name, char *out, size_t size)
{
	long n = q_sysctl(name, out, size - 1);

	if (n < 0)
		n = 0;
	out[n] = '\0';
	while (n > 0 && out[n - 1] == '\n')
		out[--n] = '\0';
	if (!n)
		strlcpy(out, "unknown", size);
}

static unsigned long num(const char *name)
{
	char b[24];
	unsigned long v = 0;
	int i;

	get(name, b, sizeof(b));
	for (i = 0; b[i] >= '0' && b[i] <= '9'; i++)
		v = v * 10 + (unsigned long)(b[i] - '0');
	return v;
}

int hixfetch_text(char *buf, size_t size)
{
	char host[24], os[24], rel[24], disp[64], de[40], plat[40];
	char info[11][72];
	unsigned long up = num("kern.uptime");
	unsigned long total = num("hw.memsize") >> 20, freem = num("hw.freemem") >> 20;
	int n = 0, i, len = 0, lines;

	get("kern.hostname", host, sizeof(host));
	get("kern.ostype", os, sizeof(os));
	get("kern.osrelease", rel, sizeof(rel));
	get("hw.display", disp, sizeof(disp));
	get("hix.desktop", de, sizeof(de));
	get("hix.platform", plat, sizeof(plat));

	snprintf(info[n++], sizeof(info[0]), "root@%s", host);
	snprintf(info[n++], sizeof(info[0]), "---------");
	snprintf(info[n++], sizeof(info[0]), "OS: %s %s", os, rel);
	snprintf(info[n++], sizeof(info[0]), "Kernel: aether + qubix + steel");
	snprintf(info[n++], sizeof(info[0]), "Platform: %s", plat);
	snprintf(info[n++], sizeof(info[0]), "Uptime: %lum %02lus", up / 60, up % 60);
	snprintf(info[n++], sizeof(info[0]), "Shell: hix sh");
	snprintf(info[n++], sizeof(info[0]), "Display: %s", disp);
	snprintf(info[n++], sizeof(info[0]), "DE: %s", de);
	snprintf(info[n++], sizeof(info[0]), "CPU: i686, %lu core", num("hw.ncpu"));
	snprintf(info[n++], sizeof(info[0]), "Memory: %lu MiB / %lu MiB",
		 total - freem, total);

	lines = n > (int)ARRAY_SIZE(logo) ? n : (int)ARRAY_SIZE(logo);
	for (i = 0; i < lines && (size_t)len < size; i++) {
		const char *l = i < (int)ARRAY_SIZE(logo) ? logo[i] : "";

		len += snprintf(buf + len, size - (size_t)len, "%-*s%s\n", LOGO_W, l,
				i < n ? info[i] : "");
	}
	return len;
}
