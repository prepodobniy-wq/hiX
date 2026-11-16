// SPDX-License-Identifier: GPL-2.0-only
/*
 * Gantry platform: builds the registry from the machine and starts the
 * drivers. This is the only part of Gantry that talks to the C kernel.
 */
extern "C" {
#include <hix/kernel.h>
#include <hix/driver.h>
#include <hix/version.h>
#include <hix/pci.h>
}
#include <gantry/gantry.h>
#include <gantry/kit.h>
#include <gantry/tweak.h>

namespace gantry {

static void scan_pci(Node *bus)
{
	int dev, fn;

	for (dev = 0; dev < 32; dev++) {
		for (fn = 0; fn < 8; fn++) {
			u32 id = pci_read32(0, dev, fn, 0);

			if ((id & 0xFFFF) == 0xFFFF) {
				if (fn == 0)
					break;		/* nothing in this slot */
				continue;
			}
			add_pci_device(bus, dev, fn, id, pci_read32(0, dev, fn, 8),
				       pci_read32(0, dev, fn, 0x10));
			if (fn == 0 && !(pci_read32(0, dev, 0, 0x0C) & 0x800000))
				break;			/* single function device */
		}
	}
}

static void build(Node *root)
{
	Node *kernel = new Node("Kernel", HIX_NAME);
	Node *drivers = new Node("Drivers", "drivers");
	Node *pci = new Node("PCIBus", "pci0");
	const struct hix_driver *d;
	unsigned int i;

	kernel->set("release", HIX_RELEASE);
	kernel->set("layers", AETHER_NAME " " QUBIX_NAME " " STEEL_NAME);
	root->attach(kernel);

	for (i = 0; (d = driver_get(i)) != nullptr; i++) {
		Node *n = new Node("HixDriver", d->name);
		char lvl[4];

		snprintf(lvl, sizeof(lvl), "%d", d->level);
		n->set("class", d->kind);
		n->set("level", lvl);
		n->set("state", driver_state_name(d->state));
		drivers->attach(n);
	}
	root->attach(drivers);

	scan_pci(pci);
	root->attach(pci);
}

}	/* namespace gantry */

extern "C" {

void gantry_boot(void)
{
	gantry::Node *root = gantry::Registry::root();
	int started;

	gantry::build(root);
	gantry::kits_init(root);
	gantry::Tweaks::boot(root);
	started = gantry::Registry::match(root);
	pr_notice("%s: %u nodes, %d of %d drivers started\n", GANTRY_NAME,
		  root->count(), started, gantry::Registry::factory_count());
}

int gantry_format(char *buf, size_t size)
{
	return gantry::Registry::format(gantry::Registry::root(), buf, size);
}

unsigned gantry_node_count(void)
{
	return gantry::Registry::root()->count();
}

}
