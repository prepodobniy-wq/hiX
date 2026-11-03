/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver framework (the hix driver model).
 *
 * A driver describes itself with a struct hix_driver and registers it with
 * HIX_DRIVER(). The linker gathers all descriptors in the .drivers section;
 * driver_start_all() walks them by init level, asks each one whether its
 * hardware is present (probe) and starts it. `devtree` in the shell prints the
 * resulting registry.
 */
#ifndef _HIX_DRIVER_H
#define _HIX_DRIVER_H

#include <hix/types.h>
#include <hix/compiler.h>

/* start order: low levels first */
#define DRV_LEVEL_CONSOLE	0	/* things printk can talk to */
#define DRV_LEVEL_PLATFORM	1	/* timers, rtc */
#define DRV_LEVEL_INPUT		2	/* keyboards and friends */
#define DRV_LEVEL_LATE		3

enum driver_state {
	DRV_IDLE,		/* not examined yet */
	DRV_ABSENT,		/* probe() said the hardware is not there */
	DRV_FAILED,		/* start() failed */
	DRV_RUNNING,
};

struct hix_driver {
	const char *name;	/* "pit", "ps2kbd" ... */
	const char *class;	/* "timer", "input", "display", ... */
	int level;
	int (*probe)(void);	/* 0: hardware present */
	int (*start)(void);	/* 0: success */
	/* filled in by the framework */
	enum driver_state state;
};

#define HIX_DRIVER(drv)						\
	static struct hix_driver *__drv_ptr_##drv __used		\
		__section(".drivers") = &(drv)

/* start every driver whose level is within [first, last] */
void driver_start_levels(int first, int last);

/* iterate the registry, @idx counts from 0; NULL at the end */
const struct hix_driver *driver_get(unsigned int idx);
const char *driver_state_name(enum driver_state state);

#endif /* _HIX_DRIVER_H */
