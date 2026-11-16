/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Kit ABI: what user programs and the kernel agree on when a program talks to
 * a Gantry service. Plain C, shared with the SDK (user/sdk).
 *
 *   h = SYS_gantry(GANTRY_LOOKUP, "kit.prop")        -> handle or -errno
 *   r = SYS_gantry(GANTRY_CALL, h | op << 16, &msg)  -> service result
 */
#ifndef _HIX_KITABI_H
#define _HIX_KITABI_H

#define SYS_gantry		600

#define GANTRY_LOOKUP		0	/* (0, name) */
#define GANTRY_CALL		1	/* (1, handle | op << 16, struct kit_msg *) */

#define KIT_DATA_MAX		120

struct kit_msg {
	unsigned int len;		/* bytes used in data, in and out */
	char data[KIT_DATA_MAX];
};

/* kit.prop: the property tree (text keys and values) */
#define KIT_PROP_GET		0	/* data = key, reply: value */
#define KIT_PROP_SET		1	/* data = "key=value" */
#define KIT_PROP_NTH		2	/* len = index, reply: "key=value" */

/* kit.system */
#define KIT_SYS_INFO		0	/* reply: one line about the system */
#define KIT_SYS_TICKS		1	/* reply: data = unsigned int ticks (100 Hz) */
#define KIT_SYS_NODES		2	/* reply: registry node count as a number */

/* kit.notify: counters for notification topics */
#define KIT_NOTIFY_POST		0	/* data = topic */
#define KIT_NOTIFY_COUNT	1	/* data = topic, result = times posted */

#endif /* _HIX_KITABI_H */
