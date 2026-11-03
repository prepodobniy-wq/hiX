/* SPDX-License-Identifier: GPL-2.0-only */
/* hix: the smallest possible PCI configuration access (mechanism #1). */
#ifndef _HIX_PCI_H
#define _HIX_PCI_H

#include <hix/types.h>

u32 pci_read32(int bus, int dev, int fn, int off);
void pci_write32(int bus, int dev, int fn, int off, u32 val);
/* first device with this id on bus 0..255; true and its place when found */
bool pci_find(u16 vendor, u16 device, int *bus, int *dev, int *fn);

#endif /* _HIX_PCI_H */
