// SPDX-License-Identifier: GPL-2.0-only
/*
 * qubix: the VFS. A tree of vnodes held in memory: directories, regular
 * files whose data lives on the kernel heap, and character devices backed by
 * driver callbacks. One global mutex protects the tree.
 */
#define pr_fmt(fmt) "qubix: vfs: " fmt

#include <hix/kernel.h>
#include <aether/kalloc.h>
#include <aether/waitq.h>
#include <qubix/vfs.h>

#define FILE_MAX_SIZE	(256 * 1024)

static struct vnode *root;
static DEFINE_MUTEX(vfs_lock);

static struct vnode *vnode_new(enum vtype type, const char *name,
			       struct vnode *parent)
{
	struct vnode *vn = kzalloc(sizeof(*vn));

	if (!vn)
		return NULL;
	vn->type = type;
	strlcpy(vn->name, name, sizeof(vn->name));
	vn->parent = parent;
	INIT_LIST_HEAD(&vn->children);
	INIT_LIST_HEAD(&vn->sibling);
	if (parent)
		list_add_tail(&vn->sibling, &parent->children);
	return vn;
}

static void vnode_free(struct vnode *vn)
{
	kfree(vn->data);
	kfree(vn);
}

static struct vnode *dir_find(struct vnode *dir, const char *name, size_t len)
{
	struct vnode *c;

	list_for_each_entry(c, &dir->children, sibling) {
		if (strlen(c->name) == len && !strncmp(c->name, name, len))
			return c;
	}
	return NULL;
}

/*
 * Resolve @path. With @want_parent the last component is not looked up:
 * the parent directory is returned and the component copied to @last.
 */
static int lookup(const char *path, bool want_parent, struct vnode **out,
		  char *last)
{
	struct vnode *cur = root;
	const char *p = path;

	if (!path || path[0] != '/')
		return -EINVAL;

	for (;;) {
		const char *end;
		size_t len;

		while (*p == '/')
			p++;
		if (!*p)
			break;
		end = p;
		while (*end && *end != '/')
			end++;
		len = (size_t)(end - p);
		if (len > VFS_NAME_MAX)
			return -ENAMETOOLONG;

		if (want_parent) {
			const char *rest = end;

			while (*rest == '/')
				rest++;
			if (!*rest) {
				memcpy(last, p, len);
				last[len] = '\0';
				*out = cur;
				return cur->type == VT_DIR ? 0 : -ENOTDIR;
			}
		}
		if (cur->type != VT_DIR)
			return -ENOTDIR;
		if (len == 1 && p[0] == '.') {
			;
		} else if (len == 2 && p[0] == '.' && p[1] == '.') {
			if (cur->parent)
				cur = cur->parent;
		} else {
			cur = dir_find(cur, p, len);
			if (!cur)
				return -ENOENT;
		}
		p = end;
	}
	if (want_parent)
		return -EINVAL;		/* "/" has no parent component */
	*out = cur;
	return 0;
}

void vfs_init(void)
{
	root = vnode_new(VT_DIR, "", NULL);
	BUG_ON(!root);
	vfs_mkdir("/dev");
	vfs_mkdir("/etc");
	vfs_mkdir("/tmp");
}

int vfs_mkdir(const char *path)
{
	struct vnode *parent;
	char name[VFS_NAME_MAX + 1];
	int err;

	mutex_lock(&vfs_lock);
	err = lookup(path, true, &parent, name);
	if (!err && dir_find(parent, name, strlen(name)))
		err = -EEXIST;
	if (!err && !vnode_new(VT_DIR, name, parent))
		err = -ENOMEM;
	mutex_unlock(&vfs_lock);
	return err;
}

int vfs_mknod_chr(const char *path, const struct vnode_ops *ops)
{
	struct vnode *parent, *vn = NULL;
	char name[VFS_NAME_MAX + 1];
	int err;

	mutex_lock(&vfs_lock);
	err = lookup(path, true, &parent, name);
	if (!err && dir_find(parent, name, strlen(name)))
		err = -EEXIST;
	if (!err) {
		vn = vnode_new(VT_CHR, name, parent);
		if (vn)
			vn->ops = ops;
		else
			err = -ENOMEM;
	}
	mutex_unlock(&vfs_lock);
	return err;
}

int vfs_unlink(const char *path)
{
	struct vnode *vn;
	int err;

	mutex_lock(&vfs_lock);
	err = lookup(path, false, &vn, NULL);
	if (!err && vn == root)
		err = -EBUSY;
	if (!err && vn->type == VT_DIR && !list_empty(&vn->children))
		err = -ENOTEMPTY;
	if (!err) {
		list_del(&vn->sibling);
		vn->unlinked = true;
		if (vn->refs == 0)
			vnode_free(vn);
	}
	mutex_unlock(&vfs_lock);
	return err;
}

int vfs_open(const char *path, int flags, struct file **out)
{
	struct vnode *vn = NULL, *parent;
	char name[VFS_NAME_MAX + 1];
	struct file *f;
	int err;

	mutex_lock(&vfs_lock);
	err = lookup(path, false, &vn, NULL);
	if (err == -ENOENT && (flags & O_CREAT)) {
		err = lookup(path, true, &parent, name);
		if (!err) {
			vn = vnode_new(VT_REG, name, parent);
			err = vn ? 0 : -ENOMEM;
		}
	} else if (!err && (flags & O_CREAT) && (flags & 0x4000)) {
		err = -EEXIST;		/* reserved for O_EXCL */
	}
	if (!err && vn->type == VT_DIR && (flags & O_ACCMODE) != O_RDONLY)
		err = -EISDIR;
	if (!err) {
		f = kzalloc(sizeof(*f));
		if (!f) {
			err = -ENOMEM;
		} else {
			if ((flags & O_TRUNC) && vn->type == VT_REG)
				vn->size = 0;
			f->vn = vn;
			f->flags = flags;
			f->refs = 1;
			vn->refs++;
			*out = f;
		}
	}
	mutex_unlock(&vfs_lock);
	return err;
}

void vfs_file_get(struct file *f)
{
	mutex_lock(&vfs_lock);
	f->refs++;
	mutex_unlock(&vfs_lock);
}

void vfs_close(struct file *f)
{
	mutex_lock(&vfs_lock);
	if (--f->refs == 0) {
		struct vnode *vn = f->vn;

		if (--vn->refs == 0 && vn->unlinked)
			vnode_free(vn);
		kfree(f);
	}
	mutex_unlock(&vfs_lock);
}

ssize_t vfs_read(struct file *f, char *buf, size_t len)
{
	struct vnode *vn = f->vn;
	ssize_t n;

	if ((f->flags & O_ACCMODE) == O_WRONLY)
		return -EBADF;
	if (vn->type == VT_DIR)
		return -EISDIR;
	if (vn->type == VT_CHR) {
		/* may sleep (tty): must not hold the VFS lock */
		n = vn->ops->read ? vn->ops->read(vn, f->pos, buf, len)
				  : -EINVAL;
		return n;
	}

	mutex_lock(&vfs_lock);
	if ((size_t)f->pos >= vn->size) {
		n = 0;
	} else {
		n = (ssize_t)min(len, vn->size - (size_t)f->pos);
		memcpy(buf, vn->data + f->pos, (size_t)n);
		f->pos += n;
	}
	mutex_unlock(&vfs_lock);
	return n;
}

ssize_t vfs_write(struct file *f, const char *buf, size_t len)
{
	struct vnode *vn = f->vn;
	ssize_t n = (ssize_t)len;

	if ((f->flags & O_ACCMODE) == O_RDONLY)
		return -EBADF;
	if (vn->type == VT_DIR)
		return -EISDIR;
	if (vn->type == VT_CHR)
		return vn->ops->write ? vn->ops->write(vn, f->pos, buf, len)
				      : -EINVAL;

	mutex_lock(&vfs_lock);
	if (f->flags & O_APPEND)
		f->pos = (off_t)vn->size;
	if ((size_t)f->pos + len > FILE_MAX_SIZE) {
		n = -EFBIG;
	} else if ((size_t)f->pos + len > vn->cap) {
		size_t cap = ALIGN((size_t)f->pos + len, 64) * 2;
		u8 *data = kmalloc(cap);

		if (!data) {
			n = -ENOMEM;
		} else {
			memset(data, 0, cap);
			if (vn->size)
				memcpy(data, vn->data, vn->size);
			kfree(vn->data);
			vn->data = data;
			vn->cap = cap;
		}
	}
	if (n >= 0) {
		if ((size_t)f->pos > vn->size)	/* hole after lseek */
			memset(vn->data + vn->size, 0,
			       (size_t)f->pos - vn->size);
		memcpy(vn->data + f->pos, buf, len);
		f->pos += (off_t)len;
		if ((size_t)f->pos > vn->size)
			vn->size = (size_t)f->pos;
	}
	mutex_unlock(&vfs_lock);
	return n;
}

off_t vfs_lseek(struct file *f, off_t off, int whence)
{
	off_t pos;

	if (f->vn->type != VT_REG)
		return -ESPIPE;
	switch (whence) {
	case SEEK_SET:
		pos = off;
		break;
	case SEEK_CUR:
		pos = f->pos + off;
		break;
	case SEEK_END:
		pos = (off_t)f->vn->size + off;
		break;
	default:
		return -EINVAL;
	}
	if (pos < 0)
		return -EINVAL;
	f->pos = pos;
	return pos;
}

int vfs_readdir(const char *path, struct dirent_rec *out, int max)
{
	struct vnode *dir, *c;
	int n = 0, err;

	mutex_lock(&vfs_lock);
	err = lookup(path, false, &dir, NULL);
	if (!err && dir->type != VT_DIR)
		err = -ENOTDIR;
	if (!err) {
		list_for_each_entry(c, &dir->children, sibling) {
			if (n >= max)
				break;
			out[n].size = (u32)c->size;
			out[n].type = c->type;
			strlcpy(out[n].name, c->name, sizeof(out[n].name));
			n++;
		}
	}
	mutex_unlock(&vfs_lock);
	return err ? err : n;
}
