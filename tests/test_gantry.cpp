// SPDX-License-Identifier: GPL-2.0-only
/* Gantry: registry nodes, properties, driver matching, the carried drivers. */
extern "C" {
#include "hosttest.h"
#include <hix/kernel.h>
#include <aether/kalloc.h>
#include <hix/version.h>
}
#include <gantry/gantry.h>
#include <gantry/kit.h>
#include <gantry/tweak.h>

using namespace gantry;

namespace {

int started_log;

/* a driver of the test's own, better than the generic one on class 0x0200 */
class FastNic : public Driver {
public:
	FastNic() : Driver("FastNic") {}
	int probe(Node *p) override { return p->get_hex("class") == 0x0200 ? 50 : -1; }
	int start(Node *p) override
	{
		started_log++;
		set("kind", "nic");
		return p->get_hex("bar0") ? 0 : -1;	/* needs a BAR to start */
	}
};
GANTRY_DRIVER(FastNic, "PCIDevice");

Node *pci_bus()
{
	return new Node("PCIBus", "pci0");
}

}	// namespace

int main()
{
	static char arena[1 << 18] __attribute__((aligned(16)));
	char text[2048];
	struct kalloc_stats st;

	kalloc_init_region(arena, sizeof(arena));

	/* nodes and properties */
	{
		Node *n = new Node("Thing", "a-rather-long-name-that-is-cut-short");

		CHECK(strlen(n->name()) < (size_t)Node::NAME_LEN);
		CHECK_STR(n->cls(), "Thing");
		CHECK(n->get("x") == nullptr);
		n->set("x", "hello");
		n->set("x", "again");			/* replaces */
		CHECK_STR(n->get("x"), "again");
		CHECK(n->prop_count() == 1);
		n->set_hex("addr", 0xDEADBEEF);
		CHECK_STR(n->get("addr"), "deadbeef");
		CHECK(n->get_hex("addr") == 0xDEADBEEF);
		CHECK(n->get_hex("x", 7) == 7);		/* not hex: the default */
		CHECK(n->get_hex("missing", 9) == 9);
		for (int i = 0; i < 20; i++) {
			char k[8];

			snprintf(k, sizeof(k), "k%d", i);
			n->set(k, "v");
		}
		CHECK(n->prop_count() == Node::MAX_PROPS);	/* extra ones are dropped */
		n->release();
	}

	/* a tree: attach order, counting, finding, ownership */
	{
		Node *root = new Node("Platform", "Root");
		Node *a = new Node("Bus", "a"), *b = new Node("Bus", "b");
		Node *c = new Node("Dev", "c");

		root->attach(a);
		root->attach(b);
		a->attach(c);
		CHECK(root->child() == a && a->next() == b && b->next() == nullptr);
		CHECK(c->parent() == a);
		CHECK(root->count() == 4);
		CHECK(root->find("c") == c && root->find("zz") == nullptr);
		CHECK(root->find_class("Bus") == a);
		root->release();			/* frees the whole tree */
	}
	kalloc_get_stats(&st);
	CHECK(st.blocks_used == 0);

	/* matching: the best score wins, only one driver per provider */
	Registry::reset();
	CHECK(Registry::factory_count() == 3);	/* GenericPCI, BochsDisplay, FastNic */
	{
		Node *root = Registry::root();
		Node *bus = pci_bus();
		int started;

		root->attach(bus);
		add_pci_device(bus, 0, 0, 0x71908086, 0x06000000, 0);	/* a host bridge */
		add_pci_device(bus, 2, 0, 0x11111234, 0x03000000, 0xFD000008);
		add_pci_device(bus, 3, 0, 0x100E8086, 0x02000000, 0xFEBC0000);	/* a nic */
		add_pci_device(bus, 4, 0, 0x100E8086, 0x02000000, 0);	/* no BAR */

		started = Registry::match(root);
		CHECK(started == 3);			/* the fourth could not start */

		Node *bridge = root->find("00.0"), *vga = root->find("02.0");
		Node *nic = root->find("03.0"), *nic2 = root->find("04.0");

		CHECK(bridge && bridge->driver());
		CHECK_STR(bridge->driver()->name(), "GenericPCI");
		CHECK_STR(bridge->driver()->get("kind"), "bridge");
		CHECK_STR(vga->driver()->name(), "BochsDisplay");	/* beats the generic one */
		CHECK_STR(vga->driver()->get("mode"), "1024x768x32");
		/* the low flag bits of the BAR are masked */
		CHECK_STR(vga->driver()->get("fb"), "fd000000");
		CHECK_STR(nic->driver()->name(), "FastNic");
		/* FastNic could not start without a BAR: the generic driver was not
		 * tried instead, the node simply stays without one */
		CHECK(nic2->driver() == nullptr);
		CHECK(started_log == 2);

		/* matching again changes nothing: nodes with a driver are skipped */
		CHECK(Registry::match(root) == 0);	/* nic2 is tried again and fails again */
		CHECK(started_log == 3);
		CHECK(root->count() == 1 + 1 + 4 + 3);

		CHECK(Registry::format(root, text, sizeof(text)) > 0);
		CHECK(strstr(text, "+-o Root  <Platform>") == text);
		CHECK(strstr(text, "    +-o BochsDisplay  <Driver kind=display "
				   "mode=1024x768x32 fb=fd000000 score=64>"));
		CHECK(strstr(text, "vendor=8086 device=7190"));
	}

	/* the text dump never overruns a small buffer */
	{
		char tiny[40];
		int n = Registry::format(Registry::root(), tiny, sizeof(tiny));

		CHECK(n > 0 && n < (int)sizeof(tiny) && tiny[n] == '\0');
	}

	/* services, notifications, properties: the Kits */
	{
		kit_msg m;
		int h;

		Services::reset();
		Notifier::reset();
		Properties::reset();
		kits_init(Registry::root());
		CHECK(Services::count() == 3);
		CHECK(Registry::root()->find("Services") != nullptr);
		CHECK(Registry::root()->find("kit.prop") != nullptr);
		CHECK(Services::lookup("kit.nothing") == -ENOENT);
		CHECK(Notifier::posted("gantry.kits") == 1);

		/* publishing the same name twice is refused */
		{
			class Dup : public Service {
			public:
				Dup() : Service("kit.prop") {}
				long call(u32, kit_msg *) override { return 0; }
			};
			Dup *d = new Dup;

			CHECK(Services::publish(d) == -EEXIST);
			d->release();
		}

		h = Services::lookup("kit.prop");
		CHECK(h >= 0);
		memset(&m, 0, sizeof(m));
		strlcpy(m.data, "hix.release", KIT_DATA_MAX);
		CHECK(Services::call(h, KIT_PROP_GET, &m) > 0);
		CHECK_STR(m.data, HIX_RELEASE);

		memset(&m, 0, sizeof(m));
		/* kernel keys are read-only */
		strlcpy(m.data, "hix.release=9", KIT_DATA_MAX);
		CHECK(Services::call(h, KIT_PROP_SET, &m) == -EACCES);
		memset(&m, 0, sizeof(m));
		strlcpy(m.data, "user.color=blue", KIT_DATA_MAX);
		CHECK(Services::call(h, KIT_PROP_SET, &m) == 0);
		memset(&m, 0, sizeof(m));
		strlcpy(m.data, "user.color", KIT_DATA_MAX);
		CHECK(Services::call(h, KIT_PROP_GET, &m) > 0);
		CHECK_STR(m.data, "blue");
		memset(&m, 0, sizeof(m));
		strlcpy(m.data, "user.missing", KIT_DATA_MAX);
		CHECK(Services::call(h, KIT_PROP_GET, &m) == -ENOENT);
		memset(&m, 0, sizeof(m));
		strlcpy(m.data, "noequals", KIT_DATA_MAX);
		CHECK(Services::call(h, KIT_PROP_SET, &m) == -EINVAL);
		memset(&m, 0, sizeof(m));
		m.len = 0;
		CHECK(Services::call(h, KIT_PROP_NTH, &m) > 0);
		CHECK_STR(m.data, "hix.release=" HIX_RELEASE);
		m.len = 999;
		CHECK(Services::call(h, KIT_PROP_NTH, &m) == -EINVAL);	/* len > data */
		m.len = 40;
		CHECK(Services::call(h, KIT_PROP_NTH, &m) == -ENOENT);
		CHECK(Services::call(h, 77, &m) == -ENOSYS);
		CHECK(Services::call(99, 0, &m) == -ENOENT);

		/* data without a terminator cannot run past the message */
		memset(&m, 'A', sizeof(m));
		m.len = KIT_DATA_MAX;
		CHECK(Services::call(h, KIT_PROP_GET, &m) == -ENOENT);

		/* too many properties */
		for (int i = 0; i < Properties::MAX + 4; i++) {
			char k[16];

			snprintf(k, sizeof(k), "user.k%d", i);
			Properties::set(k, "v");
		}
		CHECK(Properties::count() == Properties::MAX);
		CHECK(Properties::set("user.again", "v") == -ENOSPC);

		/* system kit */
		h = Services::lookup("kit.system");
		memset(&m, 0, sizeof(m));
		CHECK(Services::call(h, KIT_SYS_INFO, &m) > 0);
		CHECK(strstr(m.data, "gantry") != nullptr);
		CHECK(Services::call(h, KIT_SYS_NODES, &m) > 0);
		CHECK(simple_strtoul(m.data, nullptr, 10) == Registry::root()->count());
	}

	{
		struct Seen { int n; long arg; char topic[24]; } a = {}, all = {};
		kit_msg m;
		int ida, idall, h;

		ida = Notifier::subscribe("proc.exit", [](const char *t, long arg, void *c) {
			Seen *s = static_cast<Seen *>(c);

			s->n++;
			s->arg = arg;
			strlcpy(s->topic, t, sizeof(s->topic));
		}, &a);
		idall = Notifier::subscribe("*", [](const char *, long, void *c) {
			static_cast<Seen *>(c)->n++;
		}, &all);
		CHECK(ida >= 0 && idall >= 0);
		CHECK(Notifier::post("proc.exit", 42) == 2);
		CHECK(Notifier::post("proc.spawn", 1) == 1);	/* only the wildcard */
		CHECK(a.n == 1 && a.arg == 42 && !strcmp(a.topic, "proc.exit"));
		CHECK(all.n == 2);
		CHECK(Notifier::posted("proc.exit") == 1);
		CHECK(Notifier::unsubscribe(ida) == 0);
		CHECK(Notifier::unsubscribe(ida) == -ENOENT);
		CHECK(Notifier::post("proc.exit", 1) == 1);
		CHECK(a.n == 1);
		CHECK(Notifier::subscribe("x", nullptr, nullptr) == -EINVAL);

		/* programs can post only under user. and read any counter */
		h = Services::lookup("kit.notify");
		memset(&m, 0, sizeof(m));
		strlcpy(m.data, "proc.exit", KIT_DATA_MAX);
		CHECK(Services::call(h, KIT_NOTIFY_POST, &m) == -EACCES);
		CHECK(Services::call(h, KIT_NOTIFY_COUNT, &m) == 2);
		strlcpy(m.data, "user.ping", KIT_DATA_MAX);
		CHECK(Services::call(h, KIT_NOTIFY_POST, &m) == 1);	/* the wildcard */
		CHECK(Services::call(h, KIT_NOTIFY_COUNT, &m) == 1);

		/* the topic table is finite but never crashes */
		for (int i = 0; i < Notifier::MAX_TOPICS + 5; i++) {
			char t[16];

			snprintf(t, sizeof(t), "t%d", i);
			Notifier::post(t, 0);
		}
		CHECK(Notifier::unsubscribe(idall) == 0);
	}

	/* tweaks: hooks that can be switched at run time */
	{
		kit_msg m;
		char text[256];
		int h;

		Properties::reset();				/* the Kit blocks filled these up */
		Notifier::reset();
		CHECK(Tweaks::boot(Registry::root()) == 1);	/* procstats is on */
		CHECK(Tweaks::count() == 2);
		CHECK(Tweaks::find("procstats")->enabled());
		CHECK(!Tweaks::find("procwatch")->enabled());
		CHECK(Tweaks::find("nothing") == nullptr);
		CHECK(Registry::root()->find("procwatch") != nullptr);
		CHECK_STR(Registry::root()->find("procwatch")->get("state"), "off");

		CHECK(Notifier::post("proc.spawn", 7) == 1);	/* only procstats */
		CHECK_STR(Properties::get("sys.spawned"), "1");
		CHECK_STR(Properties::get("sys.exited"), "0");
		Notifier::post("proc.exit", 7);
		CHECK_STR(Properties::get("sys.exited"), "1");

		CHECK(Tweaks::set("procwatch", true) == 0);
		CHECK(Tweaks::set("procwatch", true) == 0);	/* idempotent */
		CHECK(Notifier::post("proc.spawn", 8) == 2);
		CHECK(Tweaks::set("procwatch", false) == 0);
		CHECK(Notifier::post("proc.spawn", 9) == 1);	/* let go again */
		CHECK_STR(Properties::get("sys.spawned"), "3");
		CHECK(Tweaks::set("procstats", false) == 0);
		CHECK(Notifier::post("proc.spawn", 9) == 0);
		CHECK(Tweaks::set("ghost", true) == -ENOENT);
		CHECK(Notifier::posted("tweak.on") >= 2);

		CHECK(Tweaks::format(text, sizeof(text)) > 0);
		CHECK(strstr(text, "procwatch") && strstr(text, "procstats"));

		/* the same through the kit.tweak service */
		h = Services::lookup("kit.tweak");
		CHECK(h >= 0);
		memset(&m, 0, sizeof(m));
		m.len = 0;
		CHECK(Services::call(h, KIT_TWEAK_NTH, &m) > 0);
		CHECK(strstr(m.data, " on ") || strstr(m.data, " off "));
		m.len = 5;
		CHECK(Services::call(h, KIT_TWEAK_NTH, &m) == -ENOENT);
		strlcpy(m.data, "procstats=on", KIT_DATA_MAX);
		CHECK(Services::call(h, KIT_TWEAK_SET, &m) == 0);
		CHECK(Tweaks::find("procstats")->enabled());
		strlcpy(m.data, "procstats=maybe", KIT_DATA_MAX);
		CHECK(Services::call(h, KIT_TWEAK_SET, &m) == -EINVAL);
		strlcpy(m.data, "nope=on", KIT_DATA_MAX);
		CHECK(Services::call(h, KIT_TWEAK_SET, &m) == -ENOENT);
		strlcpy(m.data, "nothing", KIT_DATA_MAX);
		CHECK(Services::call(h, KIT_TWEAK_SET, &m) == -EINVAL);
		Tweaks::set("procstats", false);
	}

	Registry::reset();
	Tweaks::reset();
	Services::reset();
	kalloc_get_stats(&st);
	CHECK(st.blocks_used == 0);
	CHECK(kalloc_check() == 0);

	TEST_DONE();
}
