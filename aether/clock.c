// SPDX-License-Identifier: GPL-2.0-only
/* aether: the system clock, driven by the PIT driver at HZ ticks/s. */

#include <hix/kernel.h>
#include <aether/sched.h>

volatile unsigned long jiffies;

/* timer interrupt handler body */
void clock_tick(void)
{
	jiffies++;
	sched_tick();
}

unsigned long clock_uptime_ms(void)
{
	return (jiffies / HZ) * 1000 + (jiffies % HZ) * (1000 / HZ);
}
