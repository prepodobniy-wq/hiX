/* SPDX-License-Identifier: GPL-2.0-only */
/* aether: kernel heap. */
#ifndef _AETHER_KALLOC_H
#define _AETHER_KALLOC_H

#include <hix/types.h>

void kalloc_init(void);

/* building block for kalloc_init() and the host unit tests */
void kalloc_init_region(void *base, size_t size);

void *kmalloc(size_t size);		/* 16-byte aligned, NULL on failure */
void *kzalloc(size_t size);
void kfree(void *ptr);			/* kfree(NULL) is fine */

struct kalloc_stats {
	size_t total;		/* bytes in the heap */
	size_t used;		/* bytes in live allocations (with headers) */
	size_t blocks_used;
	size_t blocks_free;
	size_t largest_free;
};

void kalloc_get_stats(struct kalloc_stats *st);

/* walks the heap and returns the number of corrupt blocks found */
int kalloc_check(void);

#endif /* _AETHER_KALLOC_H */
