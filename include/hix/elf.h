/* SPDX-License-Identifier: GPL-2.0-only */
/* Minimal ELF32 (i386, statically linked executable) reader. */
#ifndef _HIX_ELF_H
#define _HIX_ELF_H

#include <hix/types.h>

#define ELF_MAX_SEGS	4

#define ELF_PF_X	1
#define ELF_PF_W	2
#define ELF_PF_R	4

struct elf_seg {
	u32 vaddr;
	u32 offset;	/* in the image */
	u32 filesz;
	u32 memsz;
	u32 flags;	/* ELF_PF_* */
};

struct elf_info {
	u32 entry;
	int nseg;
	struct elf_seg seg[ELF_MAX_SEGS];
};

/*
 * Validate @img and describe its loadable segments. Every segment must lie in
 * [@lo, @hi). Returns 0 or -ENOEXEC.
 */
int elf_parse(const void *img, size_t len, u32 lo, u32 hi,
	      struct elf_info *out);

#endif /* _HIX_ELF_H */
