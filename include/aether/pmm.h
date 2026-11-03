/* SPDX-License-Identifier: GPL-2.0-only */
/* aether: physical page allocator. */
#ifndef _AETHER_PMM_H
#define _AETHER_PMM_H

#include <hix/types.h>

struct multiboot_info;

/* hix manages at most this much RAM; the rest is ignored */
#define PMM_MAX_RAM	0x40000000UL		/* 1 GiB */

void pmm_init(const struct multiboot_info *mbi);

/* building blocks, also used by the host unit tests */
void pmm_reset(void);
void pmm_add_region(phys_addr_t start, phys_addr_t end);	/* mark free */
void pmm_reserve(phys_addr_t start, phys_addr_t end);		/* mark used */

phys_addr_t pmm_alloc_page(void);			/* 0 on failure */
phys_addr_t pmm_alloc_contig(unsigned int pages);	/* 0 on failure */
void pmm_free_page(phys_addr_t addr);
void pmm_free_pages(phys_addr_t addr, unsigned int pages);

unsigned long pmm_free_count(void);
unsigned long pmm_total_count(void);
phys_addr_t pmm_top(void);	/* end of the highest managed page */

#endif /* _AETHER_PMM_H */
