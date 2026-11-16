/* SPDX-License-Identifier: GPL-2.0-only */
/* qubix: the initfs archive linked into the kernel, unpacked into /bin. */
#ifndef _QUBIX_INITFS_H
#define _QUBIX_INITFS_H

#include <hix/types.h>

#define INITFS_MAGIC	"HXFS"
#define INITFS_NAME_LEN	24

struct initfs_entry {
	char name[INITFS_NAME_LEN];
	u32 offset;
	u32 size;
};

/* create @dir and one file per archive entry; returns the file count or -errno */
int initfs_unpack(const void *img, size_t len, const char *dir);

#endif /* _QUBIX_INITFS_H */
