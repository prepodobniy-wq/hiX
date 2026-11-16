// SPDX-License-Identifier: GPL-2.0-only
/*
 * ELF reader and user address space bookkeeping (32-bit host program: page
 * "physical" addresses are real pointers into a static pool).
 */
#include <hix/kernel.h>
#include <hix/errno.h>
#include <hix/elf.h>
#include <aether/pmm.h>
#include <aether/uspace.h>
#include "t32.h"

#define POOL_PAGES 64
static u8 pool[POOL_PAGES * 4096] __attribute__((aligned(4096)));
static u8 used[POOL_PAGES];
static int nused;

phys_addr_t pmm_alloc_page(void)
{
	int i;

	for (i = 0; i < POOL_PAGES; i++)
		if (!used[i]) {
			used[i] = 1;
			nused++;
			return (phys_addr_t)(uintptr_t)(pool + i * 4096);
		}
	return 0;
}

void pmm_free_page(phys_addr_t a)
{
	int i = ((u8 *)(uintptr_t)a - pool) / 4096;

	if (i >= 0 && i < POOL_PAGES && used[i]) {
		used[i] = 0;
		nused--;
	}
}

/* ---- a tiny ELF built by hand ---- */
struct ehdr {
	u8 ident[16];
	u16 type, machine;
	u32 version, entry, phoff, shoff, flags;
	u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} __packed;
struct phdr {
	u32 type, offset, vaddr, paddr, filesz, memsz, flags, align;
} __packed;

static u8 image[512];

static size_t make_elf(u32 text_va, u32 data_va)
{
	struct ehdr *e = (struct ehdr *)image;
	struct phdr *p = (struct phdr *)(image + sizeof(*e));
	size_t off = sizeof(*e) + 2 * sizeof(*p);

	memset(image, 0, sizeof(image));
	e->ident[0] = 0x7f; e->ident[1] = 'E'; e->ident[2] = 'L'; e->ident[3] = 'F';
	e->ident[4] = 1; e->ident[5] = 1; e->ident[6] = 1;
	e->type = 2; e->machine = 3; e->version = 1;
	e->entry = text_va;
	e->phoff = sizeof(*e);
	e->ehsize = sizeof(*e); e->phentsize = sizeof(*p); e->phnum = 2;
	p[0].type = 1; p[0].offset = off; p[0].vaddr = text_va;
	p[0].filesz = 8; p[0].memsz = 8; p[0].flags = ELF_PF_R | ELF_PF_X;
	p[1].type = 1; p[1].offset = off + 8; p[1].vaddr = data_va;
	p[1].filesz = 4; p[1].memsz = 6000; p[1].flags = ELF_PF_R | ELF_PF_W;
	memcpy(image + off, "\xb8\x01\x00\x00\x00\xcd\x80\xf4", 8);
	memcpy(image + off + 8, "DATA", 4);
	return off + 12;
}

static void test_elf(void)
{
	struct elf_info in;
	size_t len = make_elf(0x40001000, 0x40010000);

	CHECK(elf_parse(image, len, USER_BASE, USER_TOP, &in) == 0);
	CHECK(in.nseg == 2 && in.entry == 0x40001000);
	CHECK(in.seg[1].memsz == 6000 && (in.seg[1].flags & ELF_PF_W));

	CHECK(elf_parse(image, 10, USER_BASE, USER_TOP, &in) == -ENOEXEC);
	CHECK(elf_parse(image, len - 5, USER_BASE, USER_TOP, &in) == -ENOEXEC);
	image[0] = 0;
	CHECK(elf_parse(image, len, USER_BASE, USER_TOP, &in) == -ENOEXEC);

	len = make_elf(0x1000, 0x40010000);		/* kernel addresses */
	CHECK(elf_parse(image, len, USER_BASE, USER_TOP, &in) == -ENOEXEC);

	len = make_elf(0x40001000, 0x7ffff000);		/* runs past the top */
	CHECK(elf_parse(image, len, USER_BASE, USER_TOP, &in) == -ENOEXEC);

	len = make_elf(0x40001000, 0x40010000);
	((struct ehdr *)image)->entry = 0x40010000;	/* entry in data */
	CHECK(elf_parse(image, len, USER_BASE, USER_TOP, &in) == -ENOEXEC);

	len = make_elf(0x40001000, 0x40010000);
	((struct ehdr *)image)->phoff = 0xFFFFFFF0u;	/* overflow */
	CHECK(elf_parse(image, len, USER_BASE, USER_TOP, &in) == -ENOEXEC);

	len = make_elf(0x40001000, 0x40010000);
	((struct phdr *)(image + sizeof(struct ehdr)))[1].offset = 0xFFFFFFFFu;
	CHECK(elf_parse(image, len, USER_BASE, USER_TOP, &in) == -ENOEXEC);
}

static u32 kpgd[1024] __attribute__((aligned(4096)));

static void test_space(void)
{
	struct uspace us;
	unsigned long entry, sp;
	size_t len;
	char buf[8];
	int i;

	for (i = 0; i < 1024; i++)
		kpgd[i] = (u32)(i << 22) | 3;
	kpgd[0x40000000 >> 22] = 0x1234 | 3;	/* device window: must not leak */

	CHECK(uspace_create(&us, kpgd) == 0);
	CHECK(us.pgd[0] == kpgd[0] && !(us.pgd[0] & 4));	/* shared, no U bit */
	CHECK(us.pgd[0x40000000 >> 22] == 0);
	CHECK(us.pgd[0x80000000u >> 22] == kpgd[0x80000000u >> 22]);

	CHECK(uspace_ptr(&us, 0x40000000) == NULL);
	CHECK(uspace_map(&us, 0x1000, 4096, 0) == -EINVAL);
	CHECK(uspace_map(&us, 0x7ffff000, 8192, 0) == -EINVAL);

	len = make_elf(0x40001000, 0x40010000);
	CHECK(uspace_load_elf(&us, image, len, &entry, &sp) == 0);
	CHECK(entry == 0x40001000 && sp == USER_TOP - 16);

	CHECK(uspace_copy_from(&us, buf, 0x40001000, 8) == 0);
	CHECK(memcmp(buf, "\xb8\x01\x00\x00\x00\xcd\x80\xf4", 8) == 0);
	CHECK(uspace_copy_from(&us, buf, 0x40010000, 4) == 0 &&
	      memcmp(buf, "DATA", 4) == 0);
	CHECK(uspace_ptr(&us, 0x40011000) != NULL);	/* bss, 2 pages */
	CHECK(*(u8 *)uspace_ptr(&us, 0x40011000) == 0);

	/* text is read-only, data and stack writable */
	CHECK(uspace_range_ok(&us, 0x40001000, 8, false));
	CHECK(!uspace_range_ok(&us, 0x40001000, 8, true));
	CHECK(uspace_range_ok(&us, 0x40010000, 6000, true));
	CHECK(!uspace_range_ok(&us, 0x40010000, 9000, false));	/* runs off */
	CHECK(uspace_range_ok(&us, sp - 100, 100, true));
	CHECK(!uspace_range_ok(&us, 0x3fffffff, 2, false));	/* kernel side */
	CHECK(!uspace_range_ok(&us, 0xffffff00u, 64, false));	/* wrap */
	CHECK(uspace_copy_to(&us, 0x40001000, "x", 1) == -EFAULT);
	CHECK(uspace_copy_to(&us, sp - 4, "ab", 2) == 0);
	CHECK(uspace_copy_from(&us, buf, 0x50000000, 1) == -EFAULT);

	/* a copy spanning a page boundary */
	CHECK(uspace_copy_to(&us, 0x40010ffe, "WXYZ", 4) == 0);
	CHECK(uspace_copy_from(&us, buf, 0x40010ffe, 4) == 0 &&
	      memcmp(buf, "WXYZ", 4) == 0);

	/* user pages carry the U bit, tables too */
	CHECK((us.pgd[0x40000000 >> 22] & 7) == 7);

	uspace_destroy(&us);
	CHECK(nused == 0);			/* everything came back */
}

static void test_args(void)
{
	struct uspace us;
	unsigned long entry, sp;
	size_t len = make_elf(0x40001000, 0x40010000);
	char *av[] = { "lsbin", "/bin", "x" };
	char *many[USER_ARGS_MAX + 1] = { 0 };
	char big[USER_ARGS_BYTES + 8];
	char *bigv[1] = { big };
	u32 w[5];
	char str[8];
	int i;

	CHECK(uspace_create(&us, kpgd) == 0);
	CHECK(uspace_load_elf(&us, image, len, &entry, &sp) == 0);
	CHECK(uspace_push_args(&us, &sp, 3, av) == 0);
	CHECK((sp & 15) == 0 && sp < USER_TOP - 16);
	CHECK(uspace_copy_from(&us, w, sp, sizeof(w)) == 0);
	CHECK(w[0] == 3 && w[4] == 0);			/* argc, NULL terminator */
	CHECK(uspace_copy_from(&us, str, w[1], 6) == 0 && !strcmp(str, "lsbin"));
	CHECK(uspace_copy_from(&us, str, w[2], 5) == 0 && !strcmp(str, "/bin"));
	CHECK(uspace_copy_from(&us, str, w[3], 2) == 0 && !strcmp(str, "x"));
	CHECK(w[1] > sp && w[3] < USER_TOP);

	sp = USER_TOP - 16;
	CHECK(uspace_push_args(&us, &sp, 0, av) == 0);	/* no arguments */
	CHECK(uspace_copy_from(&us, w, sp, 8) == 0 && w[0] == 0 && w[1] == 0);

	for (i = 0; i < USER_ARGS_MAX + 1; i++)
		many[i] = "a";
	CHECK(uspace_push_args(&us, &sp, USER_ARGS_MAX + 1, many) == -E2BIG);
	memset(big, 'z', sizeof(big) - 1);
	big[sizeof(big) - 1] = 0;
	CHECK(uspace_push_args(&us, &sp, 1, bigv) == -E2BIG);
	CHECK(uspace_push_args(&us, &sp, -1, av) == -E2BIG);
	uspace_destroy(&us);
	CHECK(nused == 0);
}

static void test_oom(void)
{
	struct uspace us;
	unsigned long entry, sp;
	size_t len = make_elf(0x40001000, 0x40010000);
	int hog[POOL_PAGES], n = 0, i;

	/* leave very few pages */
	for (;;) {
		phys_addr_t a = pmm_alloc_page();

		if (!a || nused > POOL_PAGES - 5)
			break;
		hog[n++] = ((u8 *)(uintptr_t)a - pool) / 4096;
	}
	CHECK(uspace_create(&us, kpgd) == 0);
	CHECK(uspace_load_elf(&us, image, len, &entry, &sp) == -ENOMEM);
	uspace_destroy(&us);
	for (i = 0; i < n; i++)
		pmm_free_page((phys_addr_t)(uintptr_t)(pool + hog[i] * 4096));
	/* the page taken by the final iteration of the loop */
	for (i = 0; i < POOL_PAGES; i++)
		if (used[i])
			pmm_free_page((phys_addr_t)(uintptr_t)(pool + i * 4096));
	CHECK(nused == 0);
}

int main(void)
{
	t_print("  [elf]\n");
	test_elf();
	t_print("  [space]\n");
	test_space();
	t_print("  [args]\n");
	test_args();
	t_print("  [oom]\n");
	test_oom();
	t_print("  %d checks, %d failed\n", tests_run, tests_failed);
	t_exit(tests_failed ? 1 : 0);
	return 0;
}

void _start(void)
{
	main();
}
