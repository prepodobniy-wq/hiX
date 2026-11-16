// SPDX-License-Identifier: GPL-2.0-only
/*
 * Gantry services: the service table, the notification center, the property
 * tree and the three built-in Kits. Pure logic over the registry, so it runs
 * in the host tests (tests/test_gantry.cpp).
 */
extern "C" {
#include <hix/kernel.h>
#include <hix/errno.h>
#include <hix/spinlock.h>
#include <hix/version.h>
}
#include <gantry/kit.h>

namespace gantry {

static DEFINE_SPINLOCK(kit_lock);

/* ---- services ---------------------------------------------------------- */

static Service *svc[Services::MAX];
static Node *svc_node;

int Services::publish(Service *s)
{
	int i;

	if (!s || lookup(s->name()) >= 0)
		return -EEXIST;
	for (i = 0; i < MAX; i++) {
		if (svc[i])
			continue;
		svc[i] = s;
		if (!svc_node)
			svc_node = Registry::root()->find("Services");
		if (svc_node)
			svc_node->attach(s);	/* the tree owns it */
		else
			s->retain();
		s->set_hex("handle", (u32)i);
		return i;
	}
	return -ENOSPC;
}

int Services::lookup(const char *name)
{
	for (int i = 0; i < MAX; i++)
		if (svc[i] && !strcmp(svc[i]->name(), name))
			return i;
	return -ENOENT;
}

long Services::call(int handle, u32 op, kit_msg *m)
{
	if (handle < 0 || handle >= MAX || !svc[handle])
		return -ENOENT;
	if (m->len > KIT_DATA_MAX)
		return -EINVAL;
	m->data[KIT_DATA_MAX - 1] = '\0';
	return static_cast<Service *>(svc[handle])->call(op, m);
}

int Services::count()
{
	int n = 0;

	for (int i = 0; i < MAX; i++)
		if (svc[i])
			n++;
	return n;
}

void Services::reset()
{
	for (int i = 0; i < MAX; i++)
		svc[i] = nullptr;	/* the registry released them with its tree */
	svc_node = nullptr;
}

/* ---- notifications ------------------------------------------------------ */

struct Sub {
	char topic[24];
	Notifier::Fn fn;
	void *ctx;
	bool used;
};
struct Topic {
	char name[24];
	unsigned count;
};

static Sub subs[Notifier::MAX_SUBS];
static Topic topics[Notifier::MAX_TOPICS];

int Notifier::subscribe(const char *topic, Fn fn, void *ctx)
{
	unsigned long flags;
	int i, ret = -ENOSPC;

	if (!fn)
		return -EINVAL;
	spin_lock_irqsave(&kit_lock, flags);
	for (i = 0; i < MAX_SUBS; i++) {
		if (subs[i].used)
			continue;
		strlcpy(subs[i].topic, topic, sizeof(subs[i].topic));
		subs[i].fn = fn;
		subs[i].ctx = ctx;
		subs[i].used = true;
		ret = i;
		break;
	}
	spin_unlock_irqrestore(&kit_lock, flags);
	return ret;
}

int Notifier::unsubscribe(int id)
{
	if (id < 0 || id >= MAX_SUBS || !subs[id].used)
		return -ENOENT;
	subs[id].used = false;
	return 0;
}

int Notifier::post(const char *topic, long arg)
{
	unsigned long flags;
	int i, called = 0;
	Topic *t = nullptr;
	Sub local[MAX_SUBS];
	int n = 0;

	spin_lock_irqsave(&kit_lock, flags);
	for (i = 0; i < MAX_TOPICS; i++)
		if (topics[i].name[0] && !strcmp(topics[i].name, topic)) {
			t = &topics[i];
			break;
		}
	if (!t)
		for (i = 0; i < MAX_TOPICS; i++)
			if (!topics[i].name[0]) {
				t = &topics[i];
				strlcpy(t->name, topic, sizeof(t->name));
				break;
			}
	if (t)
		t->count++;
	/* callbacks run outside the lock, on a copy of the table */
	for (i = 0; i < MAX_SUBS; i++)
		if (subs[i].used && (!strcmp(subs[i].topic, topic) ||
				     !strcmp(subs[i].topic, "*")))
			local[n++] = subs[i];
	spin_unlock_irqrestore(&kit_lock, flags);

	for (i = 0; i < n; i++) {
		local[i].fn(topic, arg, local[i].ctx);
		called++;
	}
	return called;
}

unsigned Notifier::posted(const char *topic)
{
	for (int i = 0; i < MAX_TOPICS; i++)
		if (topics[i].name[0] && !strcmp(topics[i].name, topic))
			return topics[i].count;
	return 0;
}

void Notifier::reset()
{
	memset(subs, 0, sizeof(subs));
	memset(topics, 0, sizeof(topics));
}

/* ---- properties --------------------------------------------------------- */

struct Entry {
	char key[Properties::KEY];
	char val[Properties::VAL];
};
static Entry props[Properties::MAX];
static int nprops;

int Properties::set(const char *key, const char *val)
{
	unsigned long flags;
	int i, ret = 0;

	if (!key[0] || strlen(key) >= (size_t)KEY || strlen(val) >= (size_t)VAL)
		return -EINVAL;
	spin_lock_irqsave(&kit_lock, flags);
	for (i = 0; i < nprops; i++)
		if (!strcmp(props[i].key, key))
			break;
	if (i == nprops) {
		if (nprops == MAX) {
			ret = -ENOSPC;
			goto out;
		}
		strlcpy(props[i].key, key, KEY);
		nprops++;
	}
	strlcpy(props[i].val, val, VAL);
out:
	spin_unlock_irqrestore(&kit_lock, flags);
	return ret;
}

const char *Properties::get(const char *key)
{
	for (int i = 0; i < nprops; i++)
		if (!strcmp(props[i].key, key))
			return props[i].val;
	return nullptr;
}

bool Properties::nth(int i, const char **key, const char **val)
{
	if (i < 0 || i >= nprops)
		return false;
	*key = props[i].key;
	*val = props[i].val;
	return true;
}

int Properties::count()
{
	return nprops;
}

void Properties::reset()
{
	nprops = 0;
	memset(props, 0, sizeof(props));
}

/* ---- the built-in Kits ---------------------------------------------------- */

namespace {

/* properties are writable only under "user." from outside the kernel */
class PropKit : public Service {
public:
	PropKit() : Service("kit.prop") {}

	long call(u32 op, kit_msg *m) override
	{
		switch (op) {
		case KIT_PROP_GET: {
			const char *v = Properties::get(m->data);

			if (!v)
				return -ENOENT;
			m->len = (unsigned)strlen(v) + 1;
			strlcpy(m->data, v, KIT_DATA_MAX);
			return (long)m->len;
		}
		case KIT_PROP_SET: {
			char *eq = strchr(m->data, '=');

			if (!eq)
				return -EINVAL;
			*eq = '\0';
			if (strncmp(m->data, "user.", 5))
				return -EACCES;
			return Properties::set(m->data, eq + 1);
		}
		case KIT_PROP_NTH: {
			const char *k, *v;

			if (!Properties::nth((int)m->len, &k, &v))
				return -ENOENT;
			m->len = (unsigned)snprintf(m->data, KIT_DATA_MAX, "%s=%s", k, v) + 1;
			return (long)m->len;
		}
		}
		return -ENOSYS;
	}
};

class SystemKit : public Service {
public:
	SystemKit() : Service("kit.system") {}

	long call(u32 op, kit_msg *m) override
	{
		switch (op) {
		case KIT_SYS_INFO:
			m->len = (unsigned)snprintf(m->data, KIT_DATA_MAX,
				"%s %s (%s, %s, %s, %s)", HIX_NAME, HIX_RELEASE,
				AETHER_NAME, QUBIX_NAME, STEEL_NAME, GANTRY_NAME) + 1;
			return (long)m->len;
		case KIT_SYS_TICKS: {
			u32 t = (u32)jiffies;

			memcpy(m->data, &t, sizeof(t));
			m->len = sizeof(t);
			return 0;
		}
		case KIT_SYS_NODES:
			m->len = (unsigned)snprintf(m->data, KIT_DATA_MAX, "%u",
						    Registry::root()->count()) + 1;
			return (long)m->len;
		}
		return -ENOSYS;
	}
};

class NotifyKit : public Service {
public:
	NotifyKit() : Service("kit.notify") {}

	long call(u32 op, kit_msg *m) override
	{
		switch (op) {
		case KIT_NOTIFY_POST:
			/* programs may only post under their own prefix */
			if (strncmp(m->data, "user.", 5))
				return -EACCES;
			return Notifier::post(m->data, 0);
		case KIT_NOTIFY_COUNT:
			return (long)Notifier::posted(m->data);
		}
		return -ENOSYS;
	}
};

}	// namespace

void kits_init(Node *root)
{
	Node *services = new Node("ServiceTable", "Services");

	root->attach(services);
	svc_node = services;
	Services::publish(new SystemKit);
	Services::publish(new PropKit);
	Services::publish(new NotifyKit);
	Properties::set("hix.release", HIX_RELEASE);
	Properties::set("hix.platform", GANTRY_NAME);
	Notifier::post("gantry.kits", Services::count());
}

}	// namespace gantry

extern "C" {

int gantry_post(const char *topic, long arg)
{
	return gantry::Notifier::post(topic, arg);
}

int gantry_prop_set(const char *key, const char *val)
{
	return gantry::Properties::set(key, val);
}

long gantry_service_lookup(const char *name)
{
	return gantry::Services::lookup(name);
}

long gantry_service_call(int handle, unsigned int op, struct kit_msg *m)
{
	return gantry::Services::call(handle, op, m);
}

}
