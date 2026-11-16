// SPDX-License-Identifier: GPL-2.0-only
/*
 * Two example tweaks. They are also the template for your own: a class
 * derived from Tweak, enable() hooks in, disable() lets go, GANTRY_TWEAK()
 * registers it.
 */
extern "C" {
#include <hix/kernel.h>
}
#include <gantry/tweak.h>

using namespace gantry;

namespace {

/* log every process start and exit to the kernel log (dmesg) */
class ProcWatch : public Tweak {
public:
	ProcWatch() : Tweak("procwatch", "log process start/exit to dmesg") {}

	int enable() override
	{
		spawn_ = Notifier::subscribe("proc.spawn", on_event, this);
		exit_ = Notifier::subscribe("proc.exit", on_event, this);
		if (spawn_ < 0 || exit_ < 0) {
			disable();
			return -ENOSPC;
		}
		return 0;
	}

	void disable() override
	{
		if (spawn_ >= 0)
			Notifier::unsubscribe(spawn_);
		if (exit_ >= 0)
			Notifier::unsubscribe(exit_);
		spawn_ = exit_ = -1;
	}

private:
	static void on_event(const char *topic, long arg, void *)
	{
		pr_info("procwatch: %s pid %ld\n", topic, arg);
	}

	int spawn_ = -1, exit_ = -1;
};
GANTRY_TWEAK(ProcWatch);

/* keep sys.spawned / sys.exited up to date in the property tree */
class ProcStats : public Tweak {
public:
	ProcStats() : Tweak("procstats", "sys.spawned / sys.exited properties", true) {}

	int enable() override
	{
		spawn_ = Notifier::subscribe("proc.spawn", on_event, this);
		exit_ = Notifier::subscribe("proc.exit", on_event, this);
		publish();
		return spawn_ < 0 || exit_ < 0 ? -ENOSPC : 0;
	}

	void disable() override
	{
		if (spawn_ >= 0)
			Notifier::unsubscribe(spawn_);
		if (exit_ >= 0)
			Notifier::unsubscribe(exit_);
		spawn_ = exit_ = -1;
	}

private:
	static void on_event(const char *topic, long, void *self)
	{
		ProcStats *s = static_cast<ProcStats *>(self);

		if (!strcmp(topic, "proc.spawn"))
			s->spawned_++;
		else
			s->exited_++;
		s->publish();
	}

	void publish()
	{
		char v[12];

		snprintf(v, sizeof(v), "%u", spawned_);
		Properties::set("sys.spawned", v);
		snprintf(v, sizeof(v), "%u", exited_);
		Properties::set("sys.exited", v);
	}

	int spawn_ = -1, exit_ = -1;
	unsigned spawned_ = 0, exited_ = 0;
};
GANTRY_TWEAK(ProcStats);

}	// namespace
