// SPDX-License-Identifier: GPL-2.0-only
/*
 * desk: System Information. Everything shown here is read from the kernel
 * with sysctlbyname(), the same values `sysctl` prints in the shell.
 */
#include <hix/kernel.h>
#include <hix/version.h>
#include <qubix/libq.h>
#include <desk/desk.h>

static unsigned long ctl_ulong(const char *name)
{
	char buf[32];

	if (q_sysctl(name, buf, sizeof(buf)) < 0)
		return 0;
	return simple_strtoul(buf, NULL, 10);
}

static void gauge(struct steel_surface *c, int x, int y, int w,
		  unsigned long used, unsigned long total, u8 attr)
{
	int fill = total ? (int)(used * (unsigned long)w / total) : 0;

	steel_rect(c, x, y, w, 1, 0xB0, STEEL_ATTR(STEEL_DGRAY, STEEL_WHITE));
	steel_rect(c, x, y, min(fill, w), 1, 0xDB, attr);
}

static void si_paint(struct win *w, struct steel_surface *c)
{
	unsigned long total = ctl_ulong("hw.memsize") >> 10;
	unsigned long freem = ctl_ulong("hw.freemem") >> 10;
	unsigned long up = ctl_ulong("kern.uptime");
	char buf[96];
	int y = 1;

	(void)w;
	steel_puts(c, 2, y++, "hix " HIX_RELEASE "  (" HIX_EXPAND ")",
		   STEEL_ATTR(STEEL_BLUE, STEEL_WHITE));
	steel_puts(c, 2, y++, "kernel: " HIX_NAME "  core: " AETHER_NAME
		   "  process: " QUBIX_NAME "  graphics: " STEEL_NAME, A_WIN);
	y++;
	snprintf(buf, sizeof(buf), "Uptime:       %lu:%02lu:%02lu", up / 3600,
		 up / 60 % 60, up % 60);
	steel_puts(c, 2, y++, buf, A_WIN);
	snprintf(buf, sizeof(buf), "Processor:    i386, %lu CPU", ctl_ulong("hw.ncpu"));
	steel_puts(c, 2, y++, buf, A_WIN);
	snprintf(buf, sizeof(buf), "Page size:    %lu bytes", ctl_ulong("hw.pagesize"));
	steel_puts(c, 2, y++, buf, A_WIN);
	y++;
	snprintf(buf, sizeof(buf), "Memory:       %lu KB total, %lu KB free", total,
		 freem);
	steel_puts(c, 2, y++, buf, A_WIN);
	gauge(c, 2, y++, c->w - 4, total - freem, total,
	      STEEL_ATTR(STEEL_BLUE, STEEL_WHITE));
	snprintf(buf, sizeof(buf), "%lu%% used", total ? (total - freem) * 100 / total : 0);
	steel_puts(c, 2, y++, buf, A_DIM);
}

const struct app app_sysinfo = {
	.name = "sysinfo",
	.title = "System Info",
	.icon = { "\xDA\xC4\xC4\xC4\xC4\xBF", "\xC0\xC4 i \xC4\xD9" },
	.icon_attr = { STEEL_ATTR(STEEL_BLUE, STEEL_WHITE),
		       STEEL_ATTR(STEEL_BLUE, STEEL_WHITE) },
	.cw = 58, .ch = 12,
	.flags = APP_SINGLE,
	.tick_ms = 1000,
	.paint = si_paint,
};
