// SPDX-License-Identifier: GPL-2.0-only
/* Tweaks: the registry of extensions and the kit.tweak service. */
extern "C" {
#include <hix/kernel.h>
#include <hix/errno.h>
}
#include <gantry/tweak.h>

namespace gantry {

static TweakFactory *factories;
static Tweak *tweaks[Tweaks::MAX];
static int ntweaks;

TweakFactory::TweakFactory(const char *n, Tweak *(*m)()) : name(n), make(m)
{
	next = factories;
	factories = this;
}

Tweak::Tweak(const char *name, const char *info, bool default_on)
	: Node("Tweak", name), on_(false), default_on_(default_on)
{
	Node::set("state", "off");
	Node::set("info", info);
}

int Tweak::set(bool on)
{
	int ret = 0;

	if (on == on_)
		return 0;
	if (on) {
		ret = enable();
		if (ret)
			return ret;
	} else {
		disable();
	}
	on_ = on;
	Node::set("state", on ? "on" : "off");
	Notifier::post(on ? "tweak.on" : "tweak.off", 0);
	return 0;
}

namespace {

/* programs reach tweaks through this service; any program may toggle them
 * until users and permissions exist */
class TweakKit : public Service {
public:
	TweakKit() : Service("kit.tweak") {}

	long call(u32 op, kit_msg *m) override
	{
		switch (op) {
		case KIT_TWEAK_NTH: {
			unsigned i = m->len;

			if (i >= (unsigned)ntweaks)
				return -ENOENT;
			m->len = (unsigned)snprintf(m->data, KIT_DATA_MAX, "%s %s %s",
				tweaks[i]->name(), tweaks[i]->get("state"),
				tweaks[i]->get("info")) + 1;
			return (long)m->len;
		}
		case KIT_TWEAK_SET: {
			char *eq = strchr(m->data, '=');

			if (!eq)
				return -EINVAL;
			*eq = '\0';
			if (!strcmp(eq + 1, "on"))
				return Tweaks::set(m->data, true);
			if (!strcmp(eq + 1, "off"))
				return Tweaks::set(m->data, false);
			return -EINVAL;
		}
		}
		return -ENOSYS;
	}
};

}	// namespace

int Tweaks::boot(Node *root)
{
	Node *table = new Node("TweakTable", "Tweaks");
	int started = 0;

	root->attach(table);
	for (TweakFactory *f = factories; f && ntweaks < MAX; f = f->next) {
		Tweak *t = f->make();

		if (!t)
			continue;
		table->attach(t);
		tweaks[ntweaks++] = t;
	}
	Services::publish(new TweakKit);
	for (int i = 0; i < ntweaks; i++)
		if (tweaks[i]->default_on() && tweaks[i]->set(true) == 0)
			started++;
	return started;
}

Tweak *Tweaks::find(const char *name)
{
	for (int i = 0; i < ntweaks; i++)
		if (!strcmp(tweaks[i]->name(), name))
			return tweaks[i];
	return nullptr;
}

int Tweaks::set(const char *name, bool on)
{
	Tweak *t = find(name);

	return t ? t->set(on) : -ENOENT;
}

int Tweaks::count()
{
	return ntweaks;
}

int Tweaks::format(char *buf, size_t size)
{
	int len = 0;

	for (int i = 0; i < ntweaks && (size_t)len < size; i++)
		len += snprintf(buf + len, size - (size_t)len, "%-12s %-3s %s\n",
				tweaks[i]->name(), tweaks[i]->get("state"),
				tweaks[i]->get("info"));
	return (size_t)len < size ? len : (int)size - 1;
}

void Tweaks::reset()
{
	for (int i = 0; i < ntweaks; i++)
		tweaks[i] = nullptr;	/* the registry tree owns them */
	ntweaks = 0;
}

}	// namespace gantry

extern "C" {

int gantry_tweak_set(const char *name, int on)
{
	return gantry::Tweaks::set(name, on != 0);
}

int gantry_tweak_format(char *buf, size_t size)
{
	return gantry::Tweaks::format(buf, size);
}

}
