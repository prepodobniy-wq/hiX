// SPDX-License-Identifier: GPL-2.0-only
/*
 * The SDK end to end on the host: the programs that scripts/mkapps.py built
 * from user/apps with the real compiler are unpacked by the real initfs code
 * into the real VFS and every image must pass the kernel's ELF checks.
 */
#include "hosttest.h"
#include <hix/kernel.h>
#include <hix/elf.h>
#include <aether/kalloc.h>
#include <aether/uspace.h>
#include <qubix/initfs.h>
#include <qubix/vfs.h>

static unsigned char *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	unsigned char *buf;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	*len = (size_t)ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc(*len);
	if (fread(buf, 1, *len, f) != *len)
		abort();
	fclose(f);
	return buf;
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "build/sdk/initfs.bin";
	size_t len, size;
	unsigned char *img = slurp(path, &len), *bad;
	struct dirent_rec recs[16];
	struct elf_info info;
	struct file *f;
	int n, i;

	if (!img) {
		printf("  cannot read %s\n", path);
		return 1;
	}
	kalloc_init_region(malloc(1 << 20), 1 << 20);
	vfs_init();

	n = initfs_unpack(img, len, "/bin");
	CHECK(n >= 2);
	n = vfs_readdir("/bin", recs, 16);
	CHECK(n >= 2);
	for (i = 0; i < n; i++) {
		char p[64];
		unsigned char *elf;

		snprintf(p, sizeof(p), "/bin/%s", recs[i].name);
		CHECK(vfs_open(p, O_RDONLY, &f) == 0);
		size = recs[i].size;
		elf = malloc(size);
		CHECK(vfs_read(f, (char *)elf, size) == (ssize_t)size);
		vfs_close(f);
		CHECK(elf_parse(elf, size, USER_BASE,
				USER_TOP - USER_STACK_PAGES * PAGE_SIZE, &info) == 0);
		CHECK(info.entry >= USER_BASE && info.nseg >= 1);
		free(elf);
	}
	CHECK(vfs_open("/bin/kitinfo", O_RDONLY, &f) == 0);
	vfs_close(f);
	CHECK(vfs_open("/bin/primes", O_RDONLY, &f) == 0);
	vfs_close(f);

	/* damaged archives are refused and create nothing dangerous */
	bad = malloc(len);
	memcpy(bad, img, len);
	memcpy(bad, "XXXX", 4);
	CHECK(initfs_unpack(bad, len, "/bin2") < 0);
	memcpy(bad, img, len);
	CHECK(initfs_unpack(bad, 6, "/bin2") < 0);		/* short header */
	CHECK(initfs_unpack(bad, 40, "/bin2") < 0);		/* table cut off */
	memcpy(bad, img, len);
	bad[8 + 24 + 4] = 0xFF;					/* size huge */
	bad[8 + 24 + 5] = 0xFF;
	bad[8 + 24 + 6] = 0xFF;
	bad[8 + 24 + 7] = 0xFF;
	CHECK(initfs_unpack(bad, len, "/bin2") < 0);
	memcpy(bad, img, len);
	bad[8] = '/';						/* path escape */
	CHECK(initfs_unpack(bad, len, "/bin3") < 0);
	free(bad);

	TEST_DONE();
}
