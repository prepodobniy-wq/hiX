// SPDX-License-Identifier: GPL-2.0-only
#include "hosttest.h"
#include <hix/kernel.h>
#include <aether/pmm.h>
#include <aether/kalloc.h>

static void test_pmm(void)
{
	phys_addr_t a, b, c, run;
	unsigned long before;

	pmm_reset();
	pmm_add_region(0x100000, 0x100000 + 64 * PAGE_SIZE);
	CHECK(pmm_free_count() == 64);
	CHECK(pmm_total_count() == 64);
	CHECK(pmm_top() == 0x100000 + 64 * PAGE_SIZE);

	pmm_reserve(0x100000, 0x100000 + 4 * PAGE_SIZE);	/* the "kernel" */
	CHECK(pmm_free_count() == 60);
	pmm_reserve(0x100000, 0x100000 + 1);			/* idempotent */
	CHECK(pmm_free_count() == 60);

	a = pmm_alloc_page();
	b = pmm_alloc_page();
	CHECK(a >= 0x104000 && b == a + PAGE_SIZE);
	CHECK(pmm_free_count() == 58);

	pmm_free_page(a);
	CHECK(pmm_free_count() == 59);
	c = pmm_alloc_page();
	CHECK(c == a);				/* freed page comes back first */

	run = pmm_alloc_contig(8);
	CHECK(run != 0 && run % PAGE_SIZE == 0);
	CHECK(pmm_free_count() == 50);
	pmm_free_pages(run, 8);
	CHECK(pmm_free_count() == 58);

	/* fragmentation: holes of 1 page cannot satisfy a 2 page request */
	before = pmm_free_count();
	CHECK(pmm_alloc_contig(1000) == 0);	/* more than exists */
	CHECK(pmm_free_count() == before);

	pmm_free_page(a + 1);			/* misaligned: ignored */
	CHECK(pmm_free_count() == before);

	/* drain everything */
	while (pmm_alloc_page())
		;
	CHECK(pmm_free_count() == 0);
	CHECK(pmm_alloc_page() == 0);
}

static void test_kalloc(void)
{
	size_t sz = 1 << 20;
	void *arena = malloc(sz + 64);
	struct kalloc_stats st;
	void *p[64];
	int i;

	kalloc_init_region(arena, sz);
	kalloc_get_stats(&st);
	CHECK(st.blocks_used == 0 && st.blocks_free == 1);
	CHECK(kalloc_check() == 0);

	p[0] = kmalloc(100);
	CHECK(p[0] && ((unsigned long)p[0] & 15) == 0);
	p[1] = kzalloc(5000);
	CHECK(p[1]);
	for (i = 0; i < 5000; i++)
		if (((char *)p[1])[i])
			break;
	CHECK(i == 5000);
	memset(p[0], 0xAB, 100);
	CHECK(kmalloc(0) == NULL);
	CHECK(kmalloc(sz * 2) == NULL);
	CHECK(kalloc_check() == 0);

	kfree(p[0]);
	kfree(p[1]);
	kalloc_get_stats(&st);
	CHECK(st.blocks_used == 0 && st.blocks_free == 1);	/* coalesced */
	CHECK(st.largest_free == st.total);

	/* many allocations, freed out of order, must coalesce back */
	for (i = 0; i < 64; i++) {
		p[i] = kmalloc(16 + i * 24);
		CHECK(p[i] != NULL);
		memset(p[i], i, 16 + i * 24);
	}
	for (i = 0; i < 64; i += 2)
		kfree(p[i]);
	CHECK(kalloc_check() == 0);
	for (i = 1; i < 64; i += 2) {
		unsigned char *q = p[i];
		int j;

		for (j = 0; j < 16 + i * 24; j++)
			if (q[j] != i)
				break;
		CHECK(j == 16 + i * 24);		/* nobody overwrote us */
		kfree(p[i]);
	}
	kalloc_get_stats(&st);
	CHECK(st.blocks_used == 0 && st.blocks_free == 1);
	CHECK(kalloc_check() == 0);

	kfree(NULL);				/* allowed */
	free(arena);
}

int main(void)
{
	test_pmm();
	test_kalloc();
	TEST_DONE();
}
