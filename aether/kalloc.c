// SPDX-License-Identifier: GPL-2.0-only
/*
 * aether: kernel heap.
 *
 * One contiguous region carved into boundary-tagged blocks. Each block starts
 * with a 16-byte header, so payloads are 16-byte aligned. Free blocks are
 * coalesced with both neighbours on kfree(). Headers carry magic numbers; a
 * bad one (double free, buffer overrun into the next header) panics at once
 * instead of corrupting memory silently.
 */
#define pr_fmt(fmt) "aether: kalloc: " fmt

#include <hix/kernel.h>
#include <hix/spinlock.h>
#include <aether/pmm.h>
#include <aether/kalloc.h>

#define MAGIC_USED	0xA110CED1U
#define MAGIC_FREE	0xF4EEB10CU
#define MIN_SPLIT	32		/* do not leave smaller free crumbs */
#define POISON_FREE	0xDD

#define HEAP_MAX	(8UL << 20)
#define HEAP_MIN	(256UL << 10)

struct block {
	u32 magic;
	u32 size;		/* total size including this header */
	u32 prev_size;		/* total size of the block before, 0 if first */
	u32 pad;
};

#define HDR		sizeof(struct block)

static u8 *heap_base;
static size_t heap_size;
static DEFINE_SPINLOCK(heap_lock);

static inline struct block *next_block(struct block *b)
{
	u8 *p = (u8 *)b + b->size;

	return p < heap_base + heap_size ? (struct block *)p : NULL;
}

static inline struct block *prev_block(struct block *b)
{
	return b->prev_size ? (struct block *)((u8 *)b - b->prev_size) : NULL;
}

static void check_block(struct block *b, const char *what)
{
	if ((b->magic != MAGIC_USED && b->magic != MAGIC_FREE) ||
	    b->size < HDR || (b->size & 15))
		panic("kalloc: heap corruption in %s (block %p, magic %08x)",
		      what, b, b->magic);
}

void kalloc_init_region(void *base, size_t size)
{
	struct block *b;

	heap_base = (u8 *)ALIGN((uintptr_t)base, 16);
	heap_size = ALIGN_DOWN(size - (heap_base - (u8 *)base), 16);

	b = (struct block *)heap_base;
	b->magic = MAGIC_FREE;
	b->size = (u32)heap_size;
	b->prev_size = 0;
}

void kalloc_init(void)
{
	unsigned long want = pmm_free_count() * PAGE_SIZE / 4;
	phys_addr_t base = 0;

	if (want > HEAP_MAX)
		want = HEAP_MAX;
	/* take the biggest contiguous run we can get, halving on failure */
	for (; want >= HEAP_MIN; want /= 2) {
		base = pmm_alloc_contig(want / PAGE_SIZE);
		if (base)
			break;
	}
	if (!base)
		panic("cannot allocate a kernel heap");

	kalloc_init_region((void *)(uintptr_t)base, want);
	pr_info("heap of %lu KiB at %08x\n", want >> 10, (u32)base);
}

void *kmalloc(size_t size)
{
	unsigned long flags;
	struct block *b;
	size_t need;
	void *ret = NULL;

	if (!size || size > heap_size)
		return NULL;
	need = ALIGN(size + HDR, 16);

	spin_lock_irqsave(&heap_lock, flags);
	for (b = (struct block *)heap_base; b; b = next_block(b)) {
		check_block(b, "kmalloc");
		if (b->magic != MAGIC_FREE || b->size < need)
			continue;

		if (b->size >= need + MIN_SPLIT) {
			struct block *rest = (struct block *)((u8 *)b + need);
			struct block *after;

			rest->magic = MAGIC_FREE;
			rest->size = b->size - (u32)need;
			rest->prev_size = (u32)need;
			b->size = (u32)need;
			after = next_block(rest);
			if (after)
				after->prev_size = rest->size;
		}
		b->magic = MAGIC_USED;
		ret = (u8 *)b + HDR;
		break;
	}
	spin_unlock_irqrestore(&heap_lock, flags);
	return ret;
}

void *kzalloc(size_t size)
{
	void *p = kmalloc(size);

	if (p)
		memset(p, 0, size);
	return p;
}

void kfree(void *ptr)
{
	unsigned long flags;
	struct block *b, *n, *p;

	if (!ptr)
		return;
	b = (struct block *)((u8 *)ptr - HDR);

	spin_lock_irqsave(&heap_lock, flags);
	if ((u8 *)b < heap_base || (u8 *)b >= heap_base + heap_size)
		panic("kfree: %p is not a heap pointer", ptr);
	if (b->magic == MAGIC_FREE)
		panic("kfree: double free of %p", ptr);
	if (b->magic != MAGIC_USED)
		panic("kfree: bad pointer %p (magic %08x)", ptr, b->magic);

	memset(ptr, POISON_FREE, b->size - HDR);
	b->magic = MAGIC_FREE;

	n = next_block(b);
	if (n) {
		check_block(n, "kfree/next");
		if (n->magic == MAGIC_FREE) {
			struct block *nn;

			b->size += n->size;
			nn = next_block(b);
			if (nn)
				nn->prev_size = b->size;
		}
	}
	p = prev_block(b);
	if (p) {
		check_block(p, "kfree/prev");
		if (p->magic == MAGIC_FREE) {
			struct block *nx;

			p->size += b->size;
			nx = next_block(p);
			if (nx)
				nx->prev_size = p->size;
		}
	}
	spin_unlock_irqrestore(&heap_lock, flags);
}

void kalloc_get_stats(struct kalloc_stats *st)
{
	unsigned long flags;
	struct block *b;

	memset(st, 0, sizeof(*st));
	st->total = heap_size;

	spin_lock_irqsave(&heap_lock, flags);
	for (b = (struct block *)heap_base; b; b = next_block(b)) {
		if (b->magic == MAGIC_USED) {
			st->used += b->size;
			st->blocks_used++;
		} else if (b->magic == MAGIC_FREE) {
			st->blocks_free++;
			if (b->size > st->largest_free)
				st->largest_free = b->size;
		} else {
			break;
		}
	}
	spin_unlock_irqrestore(&heap_lock, flags);
}

int kalloc_check(void)
{
	unsigned long flags;
	struct block *b, *prev = NULL;
	int bad = 0;

	spin_lock_irqsave(&heap_lock, flags);
	for (b = (struct block *)heap_base; b; b = next_block(b)) {
		if (b->magic != MAGIC_USED && b->magic != MAGIC_FREE) {
			bad++;
			break;		/* cannot trust the size to go on */
		}
		if (prev && b->prev_size != prev->size)
			bad++;
		if (b->size == 0)
			break;
		prev = b;
	}
	spin_unlock_irqrestore(&heap_lock, flags);
	return bad;
}
