// SPDX-License-Identifier: GPL-2.0-only
/* Driver registry and start-up, see include/hix/driver.h. */
#define pr_fmt(fmt) "io: " fmt

#include <hix/kernel.h>
#include <hix/driver.h>

/* provided by the linker script around the .drivers section */
extern struct hix_driver *__drivers_start[];
extern struct hix_driver *__drivers_end[];

static unsigned int driver_count(void)
{
	return (unsigned int)(__drivers_end - __drivers_start);
}

const char *driver_state_name(enum driver_state state)
{
	switch (state) {
	case DRV_IDLE:
		return "idle";
	case DRV_ABSENT:
		return "absent";
	case DRV_FAILED:
		return "failed";
	case DRV_RUNNING:
		return "running";
	}
	return "?";
}

const struct hix_driver *driver_get(unsigned int idx)
{
	return idx < driver_count() ? __drivers_start[idx] : NULL;
}

void driver_start_levels(int first, int last)
{
	int level;
	unsigned int i;

	for (level = first; level <= last; level++) {
		for (i = 0; i < driver_count(); i++) {
			struct hix_driver *drv = __drivers_start[i];
			int err;

			if (drv->level != level || drv->state != DRV_IDLE)
				continue;

			if (drv->probe && drv->probe() != 0) {
				drv->state = DRV_ABSENT;
				pr_info("%s (%s): not present\n", drv->name,
					drv->class);
				continue;
			}
			err = drv->start ? drv->start() : 0;
			if (err) {
				drv->state = DRV_FAILED;
				pr_err("%s (%s): start failed (%d)\n",
				       drv->name, drv->class, err);
				continue;
			}
			drv->state = DRV_RUNNING;
			pr_info("%s (%s): started\n", drv->name, drv->class);
		}
	}
}
