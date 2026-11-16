/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * qubix: the boot splash and the screensavers.
 *
 * Both are normal qubix programs: they draw with ANSI through fd 1 and read
 * the keyboard through libq, so they run in a process and never touch the
 * kernel directly.
 */
#ifndef _QUBIX_SPLASH_H
#define _QUBIX_SPLASH_H

#include <hix/types.h>

int splash_main(int argc, char **argv);
int screensaver_main(int argc, char **argv);

/* calendar fields of a UTC epoch second; wday is 0 = Sunday */
void hix_time_parts(long epoch, int *year, int *month, int *day,
		    int *hour, int *minute, int *second, int *wday);
const char *hix_month_name(int month);
const char *hix_wday_name(int wday);

/* the 39-column block clock used by the splash and the clock screensaver */
void hix_big_clock(int row, int hour, int minute, int second);

#endif /* _QUBIX_SPLASH_H */
