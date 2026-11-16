/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Build-time configuration for the boot experience, plus the runtime
 * overrides that a Multiboot loader can pass on the kernel command line.
 *
 * scripts/hixctl turns config/hix.conf into build/hixconfig.h. When hix is
 * compiled without that file (host tests, small tools), the defaults below
 * keep the code building and the behaviour identical to an old kernel.
 */
#ifndef _HIX_BOOTCONFIG_H
#define _HIX_BOOTCONFIG_H

#include <hix/types.h>

#if defined(__has_include)
# if __has_include(<hixconfig.h>)
#  include <hixconfig.h>
# endif
#endif

#ifndef HIX_CONFIG_SPLASH
#define HIX_CONFIG_SPLASH		0
#endif

#ifndef HIX_CONFIG_SPLASH_SECONDS
#define HIX_CONFIG_SPLASH_SECONDS	2
#endif

#ifndef HIX_CONFIG_QUIET
#define HIX_CONFIG_QUIET		0
#endif

#ifndef HIX_CONFIG_SCREENSAVER
#define HIX_CONFIG_SCREENSAVER		"stars"
#endif

#ifndef HIX_CONFIG_SCREENSAVER_TIMEOUT
#define HIX_CONFIG_SCREENSAVER_TIMEOUT	0
#endif

#define HIX_SCREENSAVER_NAME_MAX	16
#define HIX_CMDLINE_MAX			192

/* the live values: compiled defaults, then changed by hix.splash=... etc. */
struct hix_boot_config {
	bool splash;
	int splash_seconds;
	bool quiet;
	char screensaver[HIX_SCREENSAVER_NAME_MAX];
	int screensaver_timeout;
	char cmdline[HIX_CMDLINE_MAX];
};

extern struct hix_boot_config hix_boot;

/* parse "hix.splash=0 hix.quiet=0 hix.screensaver=matrix ..." */
void hix_bootconfig_init(const char *cmdline);

/* one line for `sysctl hix.boot`; returns the snprintf length */
int hix_bootconfig_format(char *buf, size_t size);

#endif /* _HIX_BOOTCONFIG_H */
