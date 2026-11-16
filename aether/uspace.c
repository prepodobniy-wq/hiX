// SPDX-License-Identifier: GPL-2.0-only
/*
 * aether: user address spaces. Pure page table bookkeeping, no CPU state, so
 * it is unit tested on the host (tests/test_uspace.c).
 */
#include <hix/kernel.h>
#include <hix/errno.h>
#include <aether/pmm.h>
#include <aether/uspace.h>

#define PTE_P		BIT(0)
#define PTE_W		BIT(1)
#define PTE_U		BIT(2)
#define PDE_SHIFT	22

static u32 *table_at(u32 entry)
{
	return (u32 *)(uintptr_t)(entry & PAGE_MASK);
}

static u32 *new_page(struct uspace *us)
{
	phys_addr_t pa = pmm_alloc_page();

	if (!pa)
		return NULL;
	memset((void *)(uintptr_t)pa, 0, PAGE_SIZE);
	us->npages++;
	return (u32 *)(uintptr_t)pa;
}

int uspace_create(struct uspace *us, const u32 *kernel_pgd)
{
	unsigned int i;

	memset(us, 0, sizeof(*us));
	us->pgd = new_page(us);
	if (!us->pgd)
		return -ENOMEM;
	for (i = 0; i < 1024; i++) {
		unsigned long va = (unsigned long)i << PDE_SHIFT;

		if (va >= USER_BASE && va < USER_TOP)
			continue;		/* private window */
		us->pgd[i] = kernel_pgd[i];	/* shared, supervisor only */
	}
	return 0;
}

static bool is_user_slot(unsigned int i)
{
	unsigned long va = (unsigned long)i << PDE_SHIFT;

	return va >= USER_BASE && va < USER_TOP;
}

void uspace_destroy(struct uspace *us)
{
	unsigned int i, j;

	if (!us->pgd)
		return;
	for (i = 0; i < 1024; i++) {
		u32 *pt;

		if (!is_user_slot(i) || !(us->pgd[i] & PTE_P))
			continue;
		pt = table_at(us->pgd[i]);
		for (j = 0; j < 1024; j++)
			if (pt[j] & PTE_P)
				pmm_free_page(pt[j] & PAGE_MASK);
		pmm_free_page((phys_addr_t)(uintptr_t)pt);
	}
	pmm_free_page((phys_addr_t)(uintptr_t)us->pgd);
	us->pgd = NULL;
	us->npages = 0;
}

static u32 *user_pte(const struct uspace *us, unsigned long va, bool create,
		     struct uspace *mut)
{
	u32 *pde = &us->pgd[va >> PDE_SHIFT];

	if (!(*pde & PTE_P)) {
		u32 *pt;

		if (!create)
			return NULL;
		pt = new_page(mut);
		if (!pt)
			return NULL;
		*pde = (u32)(uintptr_t)pt | PTE_P | PTE_W | PTE_U;
	}
	return table_at(*pde) + ((va >> PAGE_SHIFT) & 0x3FF);
}

int uspace_map(struct uspace *us, unsigned long va, unsigned long len,
	       unsigned int flags)
{
	unsigned long a, end;

	if (va < USER_BASE || va >= USER_TOP || len > USER_TOP - va)
		return -EINVAL;
	end = ALIGN(va + len, PAGE_SIZE);
	for (a = ALIGN_DOWN(va, PAGE_SIZE); a < end; a += PAGE_SIZE) {
		u32 *pte = user_pte(us, a, true, us);
		u32 *page;

		if (!pte)
			return -ENOMEM;
		if (*pte & PTE_P) {
			if (flags & UMAP_WRITE)
				*pte |= PTE_W;	/* segments may share a page */
			continue;
		}
		page = new_page(us);
		if (!page)
			return -ENOMEM;
		*pte = (u32)(uintptr_t)page | PTE_P | PTE_U |
		       ((flags & UMAP_WRITE) ? PTE_W : 0);
	}
	return 0;
}

void *uspace_ptr(const struct uspace *us, unsigned long va)
{
	u32 *pte;

	if (va < USER_BASE || va >= USER_TOP)
		return NULL;
	pte = user_pte(us, va, false, NULL);
	if (!pte || !(*pte & PTE_P))
		return NULL;
	return (u8 *)(uintptr_t)(*pte & PAGE_MASK) + (va & (PAGE_SIZE - 1));
}

bool uspace_range_ok(const struct uspace *us, unsigned long va,
		     unsigned long len, bool write)
{
	unsigned long a;

	if (va < USER_BASE || va >= USER_TOP || len > USER_TOP - va)
		return false;
	if (!len)
		return true;
	for (a = ALIGN_DOWN(va, PAGE_SIZE); a < va + len; a += PAGE_SIZE) {
		u32 *pte = user_pte(us, a, false, NULL);

		if (!pte || !(*pte & PTE_P) || !(*pte & PTE_U))
			return false;
		if (write && !(*pte & PTE_W))
			return false;
	}
	return true;
}

int uspace_copy_to(struct uspace *us, unsigned long va, const void *src,
		   unsigned long len)
{
	const u8 *s = src;

	if (!uspace_range_ok(us, va, len, true))
		return -EFAULT;
	while (len) {
		unsigned long n = PAGE_SIZE - (va & (PAGE_SIZE - 1));

		if (n > len)
			n = len;
		memcpy(uspace_ptr(us, va), s, n);
		va += n;
		s += n;
		len -= n;
	}
	return 0;
}

int uspace_copy_from(const struct uspace *us, void *dst, unsigned long va,
		     unsigned long len)
{
	u8 *d = dst;

	if (!uspace_range_ok(us, va, len, false))
		return -EFAULT;
	while (len) {
		unsigned long n = PAGE_SIZE - (va & (PAGE_SIZE - 1));

		if (n > len)
			n = len;
		memcpy(d, uspace_ptr(us, va), n);
		va += n;
		d += n;
		len -= n;
	}
	return 0;
}

int uspace_load_elf(struct uspace *us, const void *img, size_t len,
		    unsigned long *entry, unsigned long *sp)
{
	struct elf_info info;
	int ret, i;

	ret = elf_parse(img, len, USER_BASE, USER_TOP - USER_STACK_PAGES * PAGE_SIZE,
			&info);
	if (ret)
		return ret;
	for (i = 0; i < info.nseg; i++) {
		const struct elf_seg *s = &info.seg[i];
		unsigned long off = 0;

		/* map writable first so the copy works, no per segment W^X yet
		 * for pages shared between segments */
		ret = uspace_map(us, s->vaddr, s->memsz, UMAP_WRITE);
		if (ret)
			return ret;
		while (off < s->filesz) {
			unsigned long va = s->vaddr + off;
			unsigned long n = PAGE_SIZE - (va & (PAGE_SIZE - 1));

			if (n > s->filesz - off)
				n = s->filesz - off;
			memcpy(uspace_ptr(us, va), (const u8 *)img + s->offset + off, n);
			off += n;
		}
	}
	/* now drop write access on segments that are not writable */
	for (i = 0; i < info.nseg; i++) {
		const struct elf_seg *s = &info.seg[i];
		unsigned long a;

		if (s->flags & ELF_PF_W)
			continue;
		for (a = ALIGN_DOWN(s->vaddr, PAGE_SIZE);
		     a < s->vaddr + s->memsz; a += PAGE_SIZE) {
			int j;
			bool shared = false;

			for (j = 0; j < info.nseg; j++) {
				const struct elf_seg *o = &info.seg[j];

				if ((o->flags & ELF_PF_W) &&
				    a + PAGE_SIZE > o->vaddr &&
				    a < o->vaddr + o->memsz)
					shared = true;
			}
			if (!shared)
				*user_pte(us, a, false, NULL) &= ~PTE_W;
		}
	}
	ret = uspace_map(us, USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE,
			 USER_STACK_PAGES * PAGE_SIZE, UMAP_WRITE);
	if (ret)
		return ret;
	*entry = info.entry;
	*sp = USER_STACK_TOP - 16;	/* keep clear of the very end */
	return 0;
}

int uspace_push_args(struct uspace *us, unsigned long *sp, int argc,
		     char **argv)
{
	unsigned long top = *sp, ptrs[USER_ARGS_MAX + 1], base;
	size_t total = 0;
	int i;

	if (argc < 0 || argc > USER_ARGS_MAX)
		return -E2BIG;
	for (i = 0; i < argc; i++)
		total += strlen(argv[i]) + 1;
	if (total > USER_ARGS_BYTES)
		return -E2BIG;

	/* strings first, from the top down */
	for (i = argc - 1; i >= 0; i--) {
		size_t n = strlen(argv[i]) + 1;

		top -= n;
		if (uspace_copy_to(us, top, argv[i], n))
			return -EFAULT;
		ptrs[i] = top;
	}
	ptrs[argc] = 0;
	/* then the pointer array and argc, with the stack 16 byte aligned
	 * at the argc slot */
	base = ALIGN_DOWN(top - (unsigned long)(argc + 1) * 4 - 4, 16);
	if (uspace_copy_to(us, base, &argc, 4) ||
	    uspace_copy_to(us, base + 4, ptrs, (unsigned long)(argc + 1) * 4))
		return -EFAULT;
	*sp = base;
	return 0;
}
