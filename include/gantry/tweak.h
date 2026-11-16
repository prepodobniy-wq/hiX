/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tweaks: extensions of the system written against Gantry.
 *
 * A Tweak is a small C++ class that hooks into the running system through
 * the Notifier (events), Properties (state) and Services (calls), and that can
 * be switched on and off while hix runs. Tweaks are compiled into the kernel
 * image: drop a file with a GANTRY_TWEAK() class into gantry/tweaks/ and it
 * appears in `tweak` and in the registry. See docs/KERNEL-LIB.md.
 */
#ifndef _GANTRY_TWEAK_H
#define _GANTRY_TWEAK_H

#include <gantry/kit.h>

#ifdef __cplusplus

namespace gantry {

class Tweak : public Node {
public:
	/* @info is shown by `tweak`; @default_on: enabled at boot */
	Tweak(const char *name, const char *info, bool default_on = false);

	virtual int enable() = 0;		/* 0 or -errno */
	virtual void disable() {}

	bool enabled() const { return on_; }
	bool default_on() const { return default_on_; }
	int set(bool on);			/* idempotent */

private:
	bool on_, default_on_;
};

struct TweakFactory {
	const char *name;
	Tweak *(*make)();
	TweakFactory *next;

	TweakFactory(const char *name, Tweak *(*make)());
};

#define GANTRY_TWEAK(type)						\
	static gantry::Tweak *make_##type() { return new type; }	\
	static gantry::TweakFactory tweak_##type(#type, make_##type)

class Tweaks {
public:
	static constexpr int MAX = 16;

	/* instantiate every registered tweak under a "Tweaks" node and
	 * enable the default ones; also publishes the kit.tweak service */
	static int boot(Node *root);
	static Tweak *find(const char *name);
	static int set(const char *name, bool on);	/* 0 or -errno */
	static int count();
	static int format(char *buf, size_t size);
	static void reset();
};

}	/* namespace gantry */

#endif /* __cplusplus */

/* kit.tweak: toggling tweaks from programs */
#define KIT_TWEAK_NTH	0	/* len = index, reply "name on|off info" */
#define KIT_TWEAK_SET	1	/* data = "name=on" or "name=off" */

HIX_BEGIN_C

int gantry_tweak_set(const char *name, int on);
int gantry_tweak_format(char *buf, size_t size);

HIX_END_C

#endif /* _GANTRY_TWEAK_H */
