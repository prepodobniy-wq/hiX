/* SPDX-License-Identifier: GPL-2.0-only */
/* qubix: a small in-memory virtual file system. Absolute paths only. */
#ifndef _QUBIX_VFS_H
#define _QUBIX_VFS_H

#include <hix/types.h>
#include <hix/list.h>

#define O_RDONLY	0
#define O_WRONLY	1
#define O_RDWR		2
#define O_ACCMODE	3
#define O_CREAT		0100
#define O_TRUNC		01000
#define O_APPEND	02000

#define SEEK_SET	0
#define SEEK_CUR	1
#define SEEK_END	2

#define VFS_NAME_MAX	23
#define VFS_PATH_MAX	128

enum vtype { VT_REG, VT_DIR, VT_CHR };

struct vnode;

struct vnode_ops {
	ssize_t (*read)(struct vnode *vn, off_t pos, char *buf, size_t len);
	ssize_t (*write)(struct vnode *vn, off_t pos, const char *buf,
			 size_t len);
};

struct vnode {
	enum vtype type;
	char name[VFS_NAME_MAX + 1];
	struct vnode *parent;
	struct list_head children;
	struct list_head sibling;
	const struct vnode_ops *ops;	/* VT_CHR only */
	u8 *data;			/* VT_REG */
	size_t size, cap;
	int refs;			/* open files */
	bool unlinked;
};

struct file {
	struct vnode *vn;
	off_t pos;
	int flags;
	int refs;
};

/* one record returned by vfs_readdir() */
struct dirent_rec {
	u32 size;
	u32 type;			/* enum vtype */
	char name[VFS_NAME_MAX + 1];
};

void vfs_init(void);
int vfs_open(const char *path, int flags, struct file **out);
void vfs_file_get(struct file *f);
void vfs_close(struct file *f);
ssize_t vfs_read(struct file *f, char *buf, size_t len);
ssize_t vfs_write(struct file *f, const char *buf, size_t len);
off_t vfs_lseek(struct file *f, off_t off, int whence);
int vfs_mkdir(const char *path);
int vfs_unlink(const char *path);
int vfs_readdir(const char *path, struct dirent_rec *out, int max);

/* create a character device node (used by qubix init and tests) */
int vfs_mknod_chr(const char *path, const struct vnode_ops *ops);

#endif /* _QUBIX_VFS_H */
