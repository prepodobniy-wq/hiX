/* SPDX-License-Identifier: GPL-2.0-only */
/* aether: virtual memory (paging) for the kernel address space. */
#ifndef _AETHER_VM_H
#define _AETHER_VM_H

#include <hix/types.h>

/*
 * The kernel runs in a single identity-mapped address space. RAM is mapped
 * with 4 KiB pages so that individual pages can be protected: kernel text and
 * read-only data are write-protected, page 0 is unmapped (NULL dereferences
 * fault) and every thread stack is fenced by an unmapped guard page.
 */
void vm_init(void);

bool vm_addr_is_mapped(unsigned long addr);

/* human readable cause for a page fault at @addr, or NULL if unknown */
const char *vm_fault_reason(unsigned long addr);

/* kernel stacks: @pages usable pages with an unmapped guard page below */
void *vm_stack_alloc(unsigned int pages);
void vm_stack_free(void *base, unsigned int pages);

/* is paging on yet? (before vm_init() addresses are physical) */
bool vm_enabled(void);

#endif /* _AETHER_VM_H */
