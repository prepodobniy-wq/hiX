// SPDX-License-Identifier: GPL-2.0-only
/* CMOS real-time clock. */
#define pr_fmt(fmt) "rtc: " fmt

#include <hix/kernel.h>
#include <hix/driver.h>
#include <hix/rtc.h>
#include <asm/io.h>

#define CMOS_INDEX	0x70
#define CMOS_DATA	0x71

static bool rtc_ready;

static u8 cmos_read(u8 reg)
{
	outb(CMOS_INDEX, reg);
	return inb(CMOS_DATA);
}

static int bcd(int v)
{
	return (v & 0x0F) + (v >> 4) * 10;
}

/* days since 1970-01-01 of a civil date (Howard Hinnant's algorithm) */
static long days_from_civil(int y, int m, int d)
{
	long era, yoe, doy, doe;

	y -= m <= 2;
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = y - era * 400;
	doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

u32 rtc_mktime(const struct rtc_time *tm)
{
	long days = days_from_civil(tm->year, tm->month, tm->day);

	return (u32)(days * 86400 + tm->hour * 3600 + tm->min * 60 + tm->sec);
}

static void read_raw(u8 raw[7])
{
	raw[0] = cmos_read(0x00);	/* seconds */
	raw[1] = cmos_read(0x02);	/* minutes */
	raw[2] = cmos_read(0x04);	/* hours */
	raw[3] = cmos_read(0x07);	/* day */
	raw[4] = cmos_read(0x08);	/* month */
	raw[5] = cmos_read(0x09);	/* year */
	raw[6] = cmos_read(0x32);	/* century, 0 if not provided */
}

int rtc_read(struct rtc_time *tm)
{
	u8 a[7], b[7], status_b;
	int spin = 100000, i, century;
	long days;

	if (!rtc_ready)
		return -ENODEV;

	/* wait out an update in progress, then read twice until stable */
	while ((cmos_read(0x0A) & 0x80) && --spin)
		;
	read_raw(a);
	do {
		while ((cmos_read(0x0A) & 0x80) && --spin)
			;
		read_raw(b);
		if (!memcmp(a, b, sizeof(a)))
			break;
		memcpy(a, b, sizeof(a));
	} while (--spin);

	status_b = cmos_read(0x0B);
	if (!(status_b & 0x04))
		for (i = 0; i < 7; i++)
			if (i != 2)
				b[i] = (u8)bcd(b[i]);
	if (!(status_b & 0x04)) {
		/* hours may carry the PM flag in bit 7 in 12-hour mode */
		int pm = b[2] & 0x80;

		b[2] = (u8)bcd(b[2] & 0x7F);
		if (!(status_b & 0x02) && pm)
			b[2] = (u8)((b[2] % 12) + 12);
	} else if (!(status_b & 0x02) && (b[2] & 0x80)) {
		b[2] = (u8)(((b[2] & 0x7F) % 12) + 12);
	}

	century = b[6] ? b[6] : 20;
	tm->sec = b[0];
	tm->min = b[1];
	tm->hour = b[2];
	tm->day = b[3];
	tm->month = b[4];
	tm->year = century * 100 + b[5];

	days = days_from_civil(tm->year, tm->month, tm->day);
	tm->wday = (int)((days + 4) % 7);	/* 1970-01-01 was a Thursday */
	if (tm->wday < 0)
		tm->wday += 7;
	return 0;
}

static int rtc_start(void)
{
	rtc_ready = true;
	return 0;
}

static struct hix_driver rtc_driver = {
	.name = "cmos-rtc",
	.class = "clock",
	.level = DRV_LEVEL_PLATFORM,
	.start = rtc_start,
};
HIX_DRIVER(rtc_driver);
