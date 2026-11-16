/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * aether: user address spaces (ring 3).
 *
 * Every user process has its own page directory. The kernel part (identity
 * mapped RAM and device windows) is shared by pointing at the very same page
 * tables, always supervisor-only; the window [USER_BASE, USER_TOP) is private
 * to the process and the only place ring 3 code can touch.
 */
#ifndef _AETHER_USPACE_H
#define _AETHER_USPACE_H

#include <hix/types.h>
#include <hix/elf.h>
#include <aether/task.h>

#define USER_BASE	0x40000000UL	/* above the identity mapped RAM (1 GiB) */
#define USER_TOP	0x80000000UL
#define USER_STACK_PAGES 8
#define USER_STACK_TOP	USER_TOP

#define UMAP_WRITE	1

struct uspace {
	u32 *pgd;
	unsigned long npages;		/* pages used, tables included */
};

/* @kernel_pgd supplies the shared kernel mappings */
int uspace_create(struct uspace *us, const u32 *kernel_pgd);
void uspace_destroy(struct uspace *us);

/* back [va, va+len) with fresh zeroed pages; 0 or -errno */
int uspace_map(struct uspace *us, unsigned long va, unsigned long len,
	       unsigned int flags);
/* kernel pointer to the byte at @va, NULL when unmapped */
void *uspace_ptr(const struct uspace *us, unsigned long va);
/* is the whole range mapped for ring 3 (and writable if @write)? */
bool uspace_range_ok(const struct uspace *us, unsigned long va,
		     unsigned long len, bool write);
int uspace_copy_to(struct uspace *us, unsigned long va, const void *src,
		   unsigned long len);
int uspace_copy_from(const struct uspace *us, void *dst, unsigned long va,
		     unsigned long len);

/* load an ELF image and a stack; fills @entry and @sp */
int uspace_load_elf(struct uspace *us, const void *img, size_t len,
		    unsigned long *entry, unsigned long *sp);

/*
 * Lay out argc/argv on the user stack, System V style: at *sp on entry
 * [argc][argv[0]..argv[argc-1]][NULL] and the strings above. Updates @sp.
 */
#define USER_ARGS_MAX	16
#define USER_ARGS_BYTES	1024
int uspace_push_args(struct uspace *us, unsigned long *sp, int argc,
		     char **argv);

/* ---- arch side (aether/uspace_arch.c) ---- */
const u32 *vm_kernel_pgdir(void);
/* run an ELF image as a ring 3 process in the calling thread; only returns
 * on failure (-errno) */
int uspace_exec(const void *img, size_t len);
/* ... with arguments for main(argc, argv) */
int uspace_exec_args(void *img, size_t len, int argc, char **argv);
/* same, for a kmalloc'ed image that is freed once it has been loaded */
int uspace_exec_free(void *img, size_t len);
/* give back the address space of @t (called by its own thread when it ends) */
void uspace_release(struct thread *t);

#endif /* _AETHER_USPACE_H */
