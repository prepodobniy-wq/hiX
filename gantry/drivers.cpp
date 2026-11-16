// SPDX-License-Identifier: GPL-2.0-only
/*
 * The drivers Gantry carries itself. They only look at the properties of the
 * node they serve, so they run unchanged in the host tests.
 *
 * GenericPCI is the fallback for every PCI function: it names the kind of
 * device. BochsDisplay is better at one particular device, so it scores
 * higher there and is the one that starts.
 */
extern "C" {
#include <hix/kernel.h>
}
#include <gantry/gantry.h>

namespace gantry {

class GenericPCI : public Driver {
public:
	GenericPCI() : Driver("GenericPCI") {}

	int probe(Node *) override { return 10; }

	int start(Node *provider) override
	{
		const char *kind;

		switch (provider->get_hex("class") >> 8) {
		case 0x01: kind = "storage"; break;
		case 0x02: kind = "network"; break;
		case 0x03: kind = "display"; break;
		case 0x04: kind = "multimedia"; break;
		case 0x05: kind = "memory"; break;
		case 0x06: kind = "bridge"; break;
		case 0x0c: kind = "serial-bus"; break;
		default: kind = "other"; break;
		}
		set("kind", kind);
		return 0;
	}
};

class BochsDisplay : public Driver {
public:
	BochsDisplay() : Driver("BochsDisplay") {}

	int probe(Node *provider) override
	{
		return provider->get_hex("vendor") == 0x1234 &&
		       provider->get_hex("device") == 0x1111 ? 100 : -1;
	}

	int start(Node *provider) override
	{
		set("kind", "display");
		set("mode", "1024x768x32");
		set_hex("fb", provider->get_hex("bar0") & ~0xFu);
		return 0;
	}
};

GANTRY_DRIVER(GenericPCI, "PCIDevice");
GANTRY_DRIVER(BochsDisplay, "PCIDevice");

}	/* namespace gantry */
