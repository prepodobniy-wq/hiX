// SPDX-License-Identifier: GPL-2.0-only
/*
 * aether: physical memory manager.
 *
 * A bitmap allocator, one bit per 4 KiB page (1 = in use). It starts with
 * everything marked used; the Multiboot memory map then frees the usable
 * regions and the kernel image, boot structures and low memory are reserved
 * again.
 */
#define pr_fmt(fmt) "aether: pmm: " fmt

#include <hix/kernel.h>
#include <hix/multiboot.h>
#include <hix/spinlock.h>
#include <aether/pmm.h>

#define MAX_PAGES	(PMM_MAX_RAM / PAGE_SIZE)

extern char __kernel_end[];

static u32 bitmap[MAX_PAGES / 32];
static unsigned long free_pages;
static unsigned long total_pages;	/* pages ever freed by add_region */
static unsigned long top_page;		/* one past the highest managed page */
static unsigned long hint;		/* where the next search starts */
static DEFINE_SPINLOCK(pmm_lock);

static inline bool page_used(unsigned long pfn)
{
	return bitmap[pfn / 32] & (1U << (pfn % 32));
}

static inline void set_used(unsigned long pfn)
{
	bitmap[pfn / 32] |= 1U << (pfn % 32);
}

static inline void set_free(unsigned long pfn)
{
	bitmap[pfn / 32] &= ~(1U << (pfn % 32));
}

void pmm_reset(void)
{
	memset(bitmap, 0xFF, sizeof(bitmap));
	free_pages = 0;
	total_pages = 0;
	top_page = 0;
	hint = 0;
}

/* pages fully inside [start, end) become free */
void pmm_add_region(phys_addr_t start, phys_addr_t end)
{
	unsigned long pfn = ALIGN(start, PAGE_SIZE) / PAGE_SIZE;
	unsigned long last = ALIGN_DOWN(end, PAGE_SIZE) / PAGE_SIZE;

	if (last > MAX_PAGES)
		last = MAX_PAGES;
	for (; pfn < last; pfn++) {
		if (page_used(pfn)) {
			set_free(pfn);
			free_pages++;
			total_pages++;
		}
	}
	if (last > top_page)
		top_page = last;
}

/* pages touched at all by [start, end) become used */
void pmm_reserve(phys_addr_t start, phys_addr_t end)
{
	unsigned long pfn = start / PAGE_SIZE;
	unsigned long last = DIV_ROUND_UP(end, PAGE_SIZE);

	if (last > MAX_PAGES)
		last = MAX_PAGES;
	for (; pfn < last; pfn++) {
		if (!page_used(pfn)) {
			set_used(pfn);
			free_pages--;
		}
	}
}

phys_addr_t pmm_alloc_contig(unsigned int pages)
{
	unsigned long flags, start, run, pfn, n;
	phys_addr_t ret = 0;

	if (!pages)
		return 0;

	spin_lock_irqsave(&pmm_lock, flags);
	for (n = 0; n < 2; n++) {
		/* first pass from the hint, second from the beginning */
		unsigned long from = n ? 0 : hint;
		unsigned long to = n ? hint + pages : top_page;

		if (to > top_page)
			to = top_page;
		run = 0;
		start = 0;
		for (pfn = from; pfn < to; pfn++) {
			if (page_used(pfn)) {
				run = 0;
				continue;
			}
			if (!run)
				start = pfn;
			if (++run == pages) {
				for (pfn = start; pfn < start + pages; pfn++)
					set_used(pfn);
				free_pages -= pages;
				hint = start + pages;
				ret = (phys_addr_t)(start * PAGE_SIZE);
				goto out;
			}
		}
	}
out:
	spin_unlock_irqrestore(&pmm_lock, flags);
	return ret;
}

phys_addr_t pmm_alloc_page(void)
{
	return pmm_alloc_contig(1);
}

void pmm_free_pages(phys_addr_t addr, unsigned int pages)
{
	unsigned long flags, pfn, i;

	if (addr % PAGE_SIZE || !pages)
		return;
	pfn = addr / PAGE_SIZE;
	if (pfn + pages > top_page)
		return;

	spin_lock_irqsave(&pmm_lock, flags);
	for (i = 0; i < pages; i++) {
		if (WARN_ON(!page_used(pfn + i)))	/* double free */
			continue;
		set_free(pfn + i);
		free_pages++;
	}
	if (pfn < hint)
		hint = pfn;
	spin_unlock_irqrestore(&pmm_lock, flags);
}

void pmm_free_page(phys_addr_t addr)
{
	pmm_free_pages(addr, 1);
}

unsigned long pmm_free_count(void)
{
	return free_pages;
}

unsigned long pmm_total_count(void)
{
	return total_pages;
}

phys_addr_t pmm_top(void)
{
	return (phys_addr_t)(top_page * PAGE_SIZE);
}

void pmm_init(const struct multiboot_info *mbi)
{
	pmm_reset();

	if (mbi->flags & MB_INFO_MMAP) {
		u32 addr = mbi->mmap_addr;
		u32 end = mbi->mmap_addr + mbi->mmap_length;

		while (addr < end) {
			const struct multiboot_mmap_entry *e =
				(const void *)(uintptr_t)addr;
			u32 base = e->base_low;
			u32 len = e->length_low;

			if (e->type == MB_MMAP_AVAILABLE &&
			    e->base_high == 0 && base < PMM_MAX_RAM) {
				if (e->length_high || len > PMM_MAX_RAM - base)
					len = PMM_MAX_RAM - base;
				pmm_add_region(base, base + len);
			}
			addr += e->size + 4;
		}
		pmm_reserve(mbi->mmap_addr, mbi->mmap_addr + mbi->mmap_length);
	} else if (mbi->flags & MB_INFO_MEMORY) {
		u32 upper = mbi->mem_upper;

		if (upper > (PMM_MAX_RAM - 0x100000) / 1024)
			upper = (PMM_MAX_RAM - 0x100000) / 1024;
		pmm_add_region(0x100000, 0x100000 + upper * 1024);
	} else {
		panic("bootloader gave no memory information");
	}

	/* BIOS area, the real-mode IVT, and the kernel image itself */
	pmm_reserve(0, (phys_addr_t)(uintptr_t)__kernel_end);
	pmm_reserve((phys_addr_t)(uintptr_t)mbi,
		    (phys_addr_t)(uintptr_t)mbi + sizeof(*mbi));

	if (mbi->flags & MB_INFO_CMDLINE)
		pmm_reserve(mbi->cmdline, mbi->cmdline + 256);

	if ((mbi->flags & MB_INFO_MODS) && mbi->mods_count) {
		const struct multiboot_module *mod =
			(const void *)(uintptr_t)mbi->mods_addr;
		u32 i;

		pmm_reserve(mbi->mods_addr, mbi->mods_addr +
			    mbi->mods_count * sizeof(*mod));
		for (i = 0; i < mbi->mods_count; i++)
			pmm_reserve(mod[i].mod_start, mod[i].mod_end);
	}

	pr_info("%lu MiB managed, %lu KiB free\n",
		(unsigned long)(total_pages * PAGE_SIZE >> 20),
		(unsigned long)(free_pages * PAGE_SIZE >> 10));
}
