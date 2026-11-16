// SPDX-License-Identifier: GPL-2.0-only
/*
 * bootconfig: the runtime half of config/hix.conf.
 *
 * The generated header holds the defaults chosen on the host. A Multiboot
 * loader may override them for one boot without rebuilding:
 *
 *   hix.splash=0|on|<seconds>   splash off, on, or visible for N seconds
 *   hix.quiet=0|1               VGA log filtering
 *   hix.screensaver=NAME|off    stars, matrix, clock, blank
 *   hix.screensaver_timeout=N   idle seconds; 0 disables auto-start
 *   hix.safe=1                  no splash, no quiet, no screensaver
 *
 * Unknown keys are ignored: the command line is also used by bootloaders
 * for their own options, and a typo must not stop the machine from booting.
 */
#define pr_fmt(fmt) "boot: " fmt

#include <hix/kernel.h>
#include <hix/bootconfig.h>
#include <hix/multiboot.h>

struct hix_boot_config hix_boot = {
	.splash = HIX_CONFIG_SPLASH,
	.splash_seconds = HIX_CONFIG_SPLASH_SECONDS,
	.quiet = HIX_CONFIG_QUIET,
	.screensaver = HIX_CONFIG_SCREENSAVER,
	.screensaver_timeout = HIX_CONFIG_SCREENSAVER_TIMEOUT,
};

static bool screensaver_valid(const char *name)
{
	return !strcmp(name, "stars") || !strcmp(name, "matrix") ||
	       !strcmp(name, "clock") || !strcmp(name, "blank");
}

/* "0", "15", max 60: false (and *out untouched) on anything else */
static bool parse_seconds(const char *value, unsigned long max,
			  unsigned long *out)
{
	char *end;
	unsigned long n = simple_strtoul(value, &end, 10);

	if (end == value || *end || n > max)
		return false;
	*out = n;
	return true;
}

static void parse_token(const char *token, size_t len)
{
	char buf[96];
	char *eq, *key, *value;

	if (!len || len >= sizeof(buf))
		return;
	memcpy(buf, token, len);
	buf[len] = '\0';
	eq = strchr(buf, '=');
	if (!eq)
		return;
	*eq = '\0';
	key = buf;
	value = eq + 1;

	if (!strcmp(key, "hix.splash")) {
		if (!strcmp(value, "off") || !strcmp(value, "0")) {
			hix_boot.splash = false;
		} else if (!strcmp(value, "on") || !strcmp(value, "1")) {
			hix_boot.splash = true;
		} else {
			unsigned long secs;

			if (!parse_seconds(value, 60, &secs))
				return;
			hix_boot.splash = secs > 0;
			hix_boot.splash_seconds = (int)secs;
		}
	} else if (!strcmp(key, "hix.quiet")) {
		if (!strcmp(value, "off") || !strcmp(value, "0") ||
		    !strcmp(value, "no"))
			hix_boot.quiet = false;
		else if (!strcmp(value, "on") || !strcmp(value, "1") ||
			 !strcmp(value, "yes"))
			hix_boot.quiet = true;
	} else if (!strcmp(key, "hix.screensaver")) {
		if (!strcmp(value, "off") || !strcmp(value, "none") ||
		    !strcmp(value, "0")) {
			hix_boot.screensaver_timeout = 0;
		} else if (screensaver_valid(value)) {
			strlcpy(hix_boot.screensaver, value,
				sizeof(hix_boot.screensaver));
		}
	} else if (!strcmp(key, "hix.screensaver_timeout") ||
		   !strcmp(key, "hix.timeout")) {
		unsigned long secs;

		if (parse_seconds(value, 86400, &secs))
			hix_boot.screensaver_timeout = (int)secs;
	} else if (!strcmp(key, "hix.safe")) {
		if (!strcmp(value, "1") || !strcmp(value, "on")) {
			hix_boot.splash = false;
			hix_boot.quiet = false;
			hix_boot.screensaver_timeout = 0;
		}
	}
}

void hix_bootconfig_init(const char *cmdline)
{
	const char *p;

	hix_boot.splash = HIX_CONFIG_SPLASH;
	hix_boot.splash_seconds = HIX_CONFIG_SPLASH_SECONDS;
	hix_boot.quiet = HIX_CONFIG_QUIET;
	strlcpy(hix_boot.screensaver, HIX_CONFIG_SCREENSAVER,
		sizeof(hix_boot.screensaver));
	hix_boot.screensaver_timeout = HIX_CONFIG_SCREENSAVER_TIMEOUT;
	hix_boot.cmdline[0] = '\0';

	if (!cmdline)
		goto out;

	strlcpy(hix_boot.cmdline, cmdline, sizeof(hix_boot.cmdline));
	for (p = cmdline; *p; ) {
		const char *start;

		while (*p == ' ' || *p == '\t')
			p++;
		start = p;
		while (*p && *p != ' ' && *p != '\t')
			p++;
		parse_token(start, (size_t)(p - start));
	}

out:
	pr_info("cmdline: %s\n",
		hix_boot.cmdline[0] ? hix_boot.cmdline : "(none)");
	pr_info("boot config: splash=%s/%ds quiet=%s screensaver=%s/%ds\n",
		hix_boot.splash ? "on" : "off", hix_boot.splash_seconds,
		hix_boot.quiet ? "on" : "off", hix_boot.screensaver,
		hix_boot.screensaver_timeout);
}

int hix_bootconfig_format(char *buf, size_t size)
{
	return snprintf(buf, size,
			"splash=%s (%ds), quiet=%s, screensaver=%s (%ds), cmdline='%s'",
			hix_boot.splash ? "on" : "off", hix_boot.splash_seconds,
			hix_boot.quiet ? "on" : "off", hix_boot.screensaver,
			hix_boot.screensaver_timeout, hix_boot.cmdline);
}
