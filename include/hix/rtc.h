/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _HIX_RTC_H
#define _HIX_RTC_H

#include <hix/types.h>

struct rtc_time {
	int year, month, day;		/* month 1-12, day 1-31 */
	int hour, min, sec;
	int wday;			/* 0 = Sunday */
};

/* read the battery-backed clock; returns 0, or -ENODEV before the driver ran */
int rtc_read(struct rtc_time *tm);

/* seconds since 1970-01-01 (UTC, the RTC is assumed to run on UTC) */
u32 rtc_mktime(const struct rtc_time *tm);

#endif /* _HIX_RTC_H */
