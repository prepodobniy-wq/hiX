/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Gantry services and the Kit model.
 *
 * A Service is a named object with a call interface; the built-in Kits
 * (kit.system, kit.prop, kit.notify) are Services, and so is anything a driver
 * or a program publishes later. Services live in the registry under the
 * "Services" node, so `registry` shows them next to the hardware.
 *
 * The NotificationCenter delivers events to kernel-side subscribers and counts
 * them per topic, which is how programs in ring 3 (that cannot be called back)
 * observe the system.
 */
#ifndef _GANTRY_KIT_H
#define _GANTRY_KIT_H

#include <gantry/gantry.h>
#include <hix/kitabi.h>

#ifdef __cplusplus

namespace gantry {

class Service : public Node {
public:
	explicit Service(const char *name) : Node("Service", name) {}

	/* returns >= 0 or -errno; @m is in and out */
	virtual long call(u32 op, kit_msg *m) = 0;
};

class Services {
public:
	static constexpr int MAX = 16;

	/* the registry keeps a reference; returns the handle or -errno */
	static int publish(Service *s);
	static int lookup(const char *name);		/* handle or -ENOENT */
	static long call(int handle, u32 op, kit_msg *m);
	static int count();
	static void reset();
};

class Notifier {
public:
	typedef void (*Fn)(const char *topic, long arg, void *ctx);
	static constexpr int MAX_SUBS = 16;
	static constexpr int MAX_TOPICS = 24;

	/* topic "*" hears everything; returns an id or -errno */
	static int subscribe(const char *topic, Fn fn, void *ctx);
	static int unsubscribe(int id);
	/* returns how many subscribers were called */
	static int post(const char *topic, long arg = 0);
	static unsigned posted(const char *topic);
	static void reset();
};

class Properties {
public:
	static constexpr int MAX = 48;
	static constexpr int KEY = 24;
	static constexpr int VAL = 64;

	static int set(const char *key, const char *val);	/* 0 or -errno */
	static const char *get(const char *key);
	static bool nth(int i, const char **key, const char **val);
	static int count();
	static void reset();
};

/* the built-in Kits; called from gantry_boot() */
void kits_init(Node *root);

}	/* namespace gantry */

#endif /* __cplusplus */

HIX_BEGIN_C

/* events and properties for the C kernel */
int gantry_post(const char *topic, long arg);
int gantry_prop_set(const char *key, const char *val);
/* the system call behind SYS_gantry; @arg2 is a kernel copy of the message */
long gantry_service_lookup(const char *name);
long gantry_service_call(int handle, unsigned int op, struct kit_msg *m);

HIX_END_C

#endif /* _GANTRY_KIT_H */
