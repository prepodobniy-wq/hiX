// SPDX-License-Identifier: GPL-2.0-only
/*
 * aether: kernel virtual memory.
 *
 * RAM below the top of physical memory is identity mapped with 4 KiB pages;
 * everything above (device windows such as the local APIC or a linear
 * framebuffer) is identity mapped with uncached 4 MiB pages, so the whole
 * 4 GiB are reachable and every physical address is also a valid pointer.
 *
 * On top of that identity map we add what makes bugs visible:
 *  - page 0 is unmapped            -> NULL dereference faults
 *  - text and rodata are read-only -> wild writes into code fault
 *  - each stack has a guard page   -> stack overflow faults
 */
#define pr_fmt(fmt) "aether: vm: " fmt

#include <hix/kernel.h>
#include <asm/trap.h>
#include <aether/pmm.h>
#include <aether/vm.h>

#define PTE_PRESENT	BIT(0)
#define PTE_WRITE	BIT(1)
#define PTE_PWT		BIT(3)
#define PTE_PCD		BIT(4)
#define PTE_PSE		BIT(7)		/* in a PDE: 4 MiB page */
#define PTE_GUARD	BIT(9)		/* software: deliberately unmapped */

#define CR0_WP		BIT(16)
#define CR0_PG		BIT(31)
#define CR4_PSE		BIT(4)

#define PDE_SPAN	(1UL << 22)	/* 4 MiB */

extern char __kernel_start[];
extern char __ro_end[];
extern char boot_stack_guard[];

static u32 *kernel_pgdir;
static bool paging_on;

static u32 *pte_for(unsigned long va)
{
	u32 pde;

	if (!kernel_pgdir)
		return NULL;
	pde = kernel_pgdir[va >> 22];
	if (!(pde & PTE_PRESENT) || (pde & PTE_PSE))
		return NULL;
	return (u32 *)(uintptr_t)(pde & PAGE_MASK) + ((va >> PAGE_SHIFT) & 0x3FF);
}

static void set_guard(unsigned long va)
{
	u32 *pte = pte_for(va);

	if (!pte)
		return;
	*pte = PTE_GUARD;		/* not present, marked as intentional */
	if (paging_on)
		invlpg(va);
}

static void clear_guard(unsigned long va)
{
	u32 *pte = pte_for(va);

	if (!pte)
		return;
	*pte = (va & PAGE_MASK) | PTE_PRESENT | PTE_WRITE;
	if (paging_on)
		invlpg(va);
}

static void write_protect(unsigned long start, unsigned long end)
{
	unsigned long va;

	for (va = start; va < end; va += PAGE_SIZE) {
		u32 *pte = pte_for(va);

		if (pte)
			*pte &= ~PTE_WRITE;
	}
}

void vm_init(void)
{
	unsigned long top = ALIGN(pmm_top(), PDE_SPAN);
	unsigned long npde, i, j;
	phys_addr_t dir;

	if (top < PDE_SPAN)
		top = PDE_SPAN;
	if (top > PMM_MAX_RAM)
		top = PMM_MAX_RAM;
	npde = top / PDE_SPAN;

	dir = pmm_alloc_page();
	BUG_ON(!dir);
	kernel_pgdir = (u32 *)(uintptr_t)dir;
	memset(kernel_pgdir, 0, PAGE_SIZE);

	for (i = 0; i < npde; i++) {
		phys_addr_t pt = pmm_alloc_page();
		u32 *table = (u32 *)(uintptr_t)pt;

		if (!pt)
			panic("out of memory building page tables");
		for (j = 0; j < 1024; j++)
			table[j] = (u32)((i << 22) | (j << 12)) |
				   PTE_PRESENT | PTE_WRITE;
		kernel_pgdir[i] = pt | PTE_PRESENT | PTE_WRITE;
	}
	for (; i < 1024; i++)
		kernel_pgdir[i] = (u32)(i << 22) | PTE_PRESENT | PTE_WRITE |
				  PTE_PSE | PTE_PCD | PTE_PWT;

	write_protect((unsigned long)__kernel_start, (unsigned long)__ro_end);
	set_guard(0);
	set_guard((unsigned long)boot_stack_guard);

	write_cr4(read_cr4() | CR4_PSE);
	write_cr3((unsigned long)kernel_pgdir);
	write_cr0(read_cr0() | CR0_PG | CR0_WP);
	paging_on = true;
	gdt_set_df_cr3((unsigned long)kernel_pgdir);

	pr_info("paging on, %lu MiB mapped in 4K pages, %lu KiB of tables\n",
		top >> 20, (npde + 1) * PAGE_SIZE >> 10);
}

bool vm_enabled(void)
{
	return paging_on;
}

bool vm_addr_is_mapped(unsigned long addr)
{
	u32 pde;
	u32 *pte;

	if (!paging_on)
		return true;
	pde = kernel_pgdir[addr >> 22];
	if (!(pde & PTE_PRESENT))
		return false;
	if (pde & PTE_PSE)
		return true;
	pte = pte_for(addr);
	return pte && (*pte & PTE_PRESENT);
}

const char *vm_fault_reason(unsigned long addr)
{
	u32 *pte;

	if (addr < PAGE_SIZE)
		return "NULL pointer dereference";
	pte = pte_for(addr);
	if (pte && (*pte & PTE_GUARD))
		return "kernel stack overflow (guard page)";
	if (pte && (*pte & PTE_PRESENT) && !(*pte & PTE_WRITE))
		return "write to read-only memory (kernel text/rodata)";
	return NULL;
}

void *vm_stack_alloc(unsigned int pages)
{
	phys_addr_t base = pmm_alloc_contig(pages + 1);

	if (!base)
		return NULL;
	set_guard(base);
	return (void *)(uintptr_t)(base + PAGE_SIZE);
}

void vm_stack_free(void *stack, unsigned int pages)
{
	phys_addr_t base = (phys_addr_t)(uintptr_t)stack - PAGE_SIZE;

	clear_guard(base);
	pmm_free_pages(base, pages + 1);
}
