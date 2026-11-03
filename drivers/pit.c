// SPDX-License-Identifier: GPL-2.0-only
/* 8253/8254 programmable interval timer: the system tick (HZ per second). */
#define pr_fmt(fmt) "pit: " fmt

#include <hix/kernel.h>
#include <hix/driver.h>
#include <asm/io.h>
#include <asm/trap.h>
#include <aether/sched.h>

#define PIT_CHANNEL0	0x40
#define PIT_COMMAND	0x43
#define PIT_BASE_HZ	1193182

static void pit_irq(int irq, void *dev)
{
	(void)irq;
	(void)dev;
	clock_tick();
}

static int pit_start(void)
{
	u32 divisor = PIT_BASE_HZ / HZ;

	outb(PIT_COMMAND, 0x36);		/* ch0, lo/hi byte, rate generator */
	outb(PIT_CHANNEL0, divisor & 0xFF);
	outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
	return request_irq(0, pit_irq, "timer", NULL);
}

static struct hix_driver pit_driver = {
	.name = "pit",
	.class = "timer",
	.level = DRV_LEVEL_PLATFORM,
	.start = pit_start,
};
HIX_DRIVER(pit_driver);
