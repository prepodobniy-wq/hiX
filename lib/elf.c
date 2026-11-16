// SPDX-License-Identifier: GPL-2.0-only
/*
 * ELF32 executable reader. It trusts nothing in the image: every offset and
 * size is range checked with overflow-safe arithmetic.
 */
#include <hix/kernel.h>
#include <hix/errno.h>
#include <hix/elf.h>

#define EI_NIDENT	16
#define ET_EXEC		2
#define EM_386		3
#define PT_LOAD		1
#define PHNUM_MAX	16

struct elf32_ehdr {
	u8 ident[EI_NIDENT];
	u16 type, machine;
	u32 version, entry, phoff, shoff, flags;
	u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} __packed;

struct elf32_phdr {
	u32 type, offset, vaddr, paddr, filesz, memsz, flags, align;
} __packed;

int elf_parse(const void *img, size_t len, u32 lo, u32 hi,
	      struct elf_info *out)
{
	const u8 *base = img;
	const struct elf32_ehdr *eh = img;
	unsigned int i;
	bool entry_ok = false;

	memset(out, 0, sizeof(*out));
	if (len < sizeof(*eh))
		return -ENOEXEC;
	if (eh->ident[0] != 0x7f || eh->ident[1] != 'E' ||
	    eh->ident[2] != 'L' || eh->ident[3] != 'F')
		return -ENOEXEC;
	if (eh->ident[4] != 1 || eh->ident[5] != 1)	/* 32-bit, little endian */
		return -ENOEXEC;
	if (eh->type != ET_EXEC || eh->machine != EM_386)
		return -ENOEXEC;
	if (eh->phentsize != sizeof(struct elf32_phdr) || !eh->phnum ||
	    eh->phnum > PHNUM_MAX)
		return -ENOEXEC;
	if (eh->phoff > len ||
	    (size_t)eh->phnum * sizeof(struct elf32_phdr) > len - eh->phoff)
		return -ENOEXEC;

	for (i = 0; i < eh->phnum; i++) {
		struct elf32_phdr ph;
		struct elf_seg *s;

		memcpy(&ph, base + eh->phoff + i * sizeof(ph), sizeof(ph));
		if (ph.type != PT_LOAD)
			continue;
		if (out->nseg == ELF_MAX_SEGS)
			return -ENOEXEC;
		if (ph.filesz > ph.memsz || ph.offset > len ||
		    ph.filesz > len - ph.offset)
			return -ENOEXEC;
		if (ph.vaddr < lo || ph.vaddr >= hi || ph.memsz > hi - ph.vaddr)
			return -ENOEXEC;
		if (!ph.memsz)
			continue;
		s = &out->seg[out->nseg++];
		s->vaddr = ph.vaddr;
		s->offset = ph.offset;
		s->filesz = ph.filesz;
		s->memsz = ph.memsz;
		s->flags = ph.flags;
		if (eh->entry >= ph.vaddr && eh->entry - ph.vaddr < ph.memsz &&
		    (ph.flags & ELF_PF_X))
			entry_ok = true;
	}
	if (!out->nseg || !entry_ok)
		return -ENOEXEC;
	out->entry = eh->entry;
	return 0;
}
