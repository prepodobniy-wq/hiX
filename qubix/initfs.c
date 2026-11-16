// SPDX-License-Identifier: GPL-2.0-only
/*
 * qubix: initfs. scripts/mkapps.py packs the SDK programs into one archive
 * that is linked into the kernel; at boot it is unpacked into /bin, so user
 * programs are ordinary files that `ls`, `cat` and the shell can see.
 */
#include <hix/kernel.h>
#include <hix/errno.h>
#include <qubix/initfs.h>
#include <qubix/vfs.h>

int initfs_unpack(const void *img, size_t len, const char *dir)
{
	const u8 *base = img;
	u32 count, i;
	int made = 0;

	if (len < 8 || memcmp(base, INITFS_MAGIC, 4))
		return -ENOEXEC;
	memcpy(&count, base + 4, sizeof(count));
	if (count > 64 || 8 + (size_t)count * sizeof(struct initfs_entry) > len)
		return -ENOEXEC;
	vfs_mkdir(dir);
	for (i = 0; i < count; i++) {
		struct initfs_entry e;
		char path[VFS_PATH_MAX];
		struct file *f;

		memcpy(&e, base + 8 + i * sizeof(e), sizeof(e));
		e.name[INITFS_NAME_LEN - 1] = '\0';
		if (!e.name[0] || strchr(e.name, '/') || strlen(e.name) > VFS_NAME_MAX)
			return -ENOEXEC;
		if (e.offset > len || e.size > len - e.offset)
			return -ENOEXEC;
		snprintf(path, sizeof(path), "%s/%s", dir, e.name);
		if (vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC, &f))
			continue;
		vfs_write(f, (const char *)base + e.offset, e.size);
		vfs_close(f);
		made++;
	}
	return made;
}
