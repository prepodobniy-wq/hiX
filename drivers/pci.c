// SPDX-License-Identifier: GPL-2.0-only
/* PCI configuration space through ports 0xCF8/0xCFC. */
#include <hix/kernel.h>
#include <hix/pci.h>
#include <asm/io.h>

#define PCI_ADDR	0xCF8
#define PCI_DATA	0xCFC

static u32 cfg_addr(int bus, int dev, int fn, int off)
{
	return 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) |
	       ((u32)fn << 8) | ((u32)off & 0xFC);
}

u32 pci_read32(int bus, int dev, int fn, int off)
{
	outl(PCI_ADDR, cfg_addr(bus, dev, fn, off));
	return inl(PCI_DATA);
}

void pci_write32(int bus, int dev, int fn, int off, u32 val)
{
	outl(PCI_ADDR, cfg_addr(bus, dev, fn, off));
	outl(PCI_DATA, val);
}

bool pci_find(u16 vendor, u16 device, int *bus, int *dev, int *fn)
{
	int b, d, f;

	for (b = 0; b < 256; b++) {
		for (d = 0; d < 32; d++) {
			for (f = 0; f < 8; f++) {
				u32 id = pci_read32(b, d, f, 0);

				if ((id & 0xFFFF) == 0xFFFF) {
					if (f == 0)
						break;	/* no device here */
					continue;
				}
				if ((id & 0xFFFF) == vendor && (id >> 16) == device) {
					*bus = b;
					*dev = d;
					*fn = f;
					return true;
				}
				if (f == 0 && !(pci_read32(b, d, 0, 0x0C) & 0x00800000))
					break;		/* not multi-function */
			}
		}
		/* QEMU puts everything on bus 0: do not scan the empty rest */
		if (b == 0)
			break;
	}
	return false;
}
