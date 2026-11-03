// SPDX-License-Identifier: GPL-2.0-only
#include "hosttest.h"
#include <hix/kernel.h>
#include <aether/kalloc.h>
#include <qubix/vfs.h>

static ssize_t dev_write(struct vnode *vn, off_t pos, const char *buf,
			 size_t len)
{
	(void)vn;
	(void)pos;
	(void)buf;
	return (ssize_t)len;
}

static const struct vnode_ops dev_ops = { .write = dev_write };

int main(void)
{
	struct file *f, *g;
	struct dirent_rec recs[16];
	char buf[64];
	int n;

	kalloc_init_region(malloc(1 << 20), 1 << 20);
	vfs_init();

	CHECK(vfs_open("/nothing", O_RDONLY, &f) == -ENOENT);
	CHECK(vfs_open("relative", O_RDONLY, &f) == -EINVAL);

	CHECK(vfs_open("/tmp/a.txt", O_RDWR | O_CREAT, &f) == 0);
	CHECK(vfs_write(f, "hello world", 11) == 11);
	CHECK(vfs_lseek(f, 0, SEEK_SET) == 0);
	memset(buf, 0, sizeof(buf));
	CHECK(vfs_read(f, buf, sizeof(buf)) == 11);
	CHECK_STR(buf, "hello world");
	CHECK(vfs_read(f, buf, sizeof(buf)) == 0);		/* EOF */
	CHECK(vfs_lseek(f, -5, SEEK_END) == 6);
	CHECK(vfs_read(f, buf, 5) == 5 && !memcmp(buf, "world", 5));
	CHECK(vfs_lseek(f, -100, SEEK_CUR) == -EINVAL);
	vfs_close(f);

	/* append and truncate */
	CHECK(vfs_open("/tmp/a.txt", O_WRONLY | O_APPEND, &f) == 0);
	CHECK(vfs_write(f, "!", 1) == 1);
	vfs_close(f);
	CHECK(vfs_open("/tmp/a.txt", O_RDONLY, &f) == 0);
	CHECK(vfs_read(f, buf, sizeof(buf)) == 12 && buf[11] == '!');
	CHECK(vfs_write(f, "x", 1) == -EBADF);			/* read-only fd */
	vfs_close(f);
	CHECK(vfs_open("/tmp/a.txt", O_WRONLY | O_TRUNC, &f) == 0);
	vfs_close(f);
	CHECK(vfs_open("/tmp/a.txt", O_RDONLY, &f) == 0);
	CHECK(vfs_read(f, buf, sizeof(buf)) == 0);
	vfs_close(f);

	/* writing past the end leaves a zero-filled hole */
	CHECK(vfs_open("/tmp/h", O_RDWR | O_CREAT, &f) == 0);
	CHECK(vfs_lseek(f, 100, SEEK_SET) == 100);
	CHECK(vfs_write(f, "z", 1) == 1);
	CHECK(vfs_lseek(f, 0, SEEK_SET) == 0);
	CHECK(vfs_read(f, buf, 64) == 64 && buf[0] == 0 && buf[63] == 0);
	vfs_close(f);

	/* directories */
	CHECK(vfs_mkdir("/tmp/sub") == 0);
	CHECK(vfs_mkdir("/tmp/sub") == -EEXIST);
	CHECK(vfs_mkdir("/nonexistent/x") == -ENOENT);
	CHECK(vfs_open("/tmp/sub/f", O_WRONLY | O_CREAT, &f) == 0);
	vfs_close(f);
	CHECK(vfs_unlink("/tmp/sub") == -ENOTEMPTY);
	CHECK(vfs_open("/tmp/sub", O_WRONLY, &f) == -EISDIR);
	CHECK(vfs_open("/tmp/a.txt/x", O_RDONLY, &f) == -ENOTDIR);
	CHECK(vfs_open("/tmp/sub/../a.txt", O_RDONLY, &f) == 0);	/* .. */
	vfs_close(f);

	n = vfs_readdir("/tmp", recs, 16);
	CHECK(n == 3);					/* a.txt h sub */
	CHECK(vfs_readdir("/tmp/a.txt", recs, 16) == -ENOTDIR);

	/* unlink while open: data stays readable until the last close */
	CHECK(vfs_open("/tmp/u", O_RDWR | O_CREAT, &f) == 0);
	vfs_write(f, "keep", 4);
	CHECK(vfs_unlink("/tmp/u") == 0);
	CHECK(vfs_open("/tmp/u", O_RDONLY, &g) == -ENOENT);
	vfs_lseek(f, 0, SEEK_SET);
	CHECK(vfs_read(f, buf, 4) == 4 && !memcmp(buf, "keep", 4));
	vfs_close(f);
	CHECK(vfs_unlink("/") == -EBUSY);

	/* devices */
	CHECK(vfs_mknod_chr("/dev/zero_sink", &dev_ops) == 0);
	CHECK(vfs_open("/dev/zero_sink", O_WRONLY, &f) == 0);
	CHECK(vfs_write(f, "abc", 3) == 3);
	CHECK(vfs_lseek(f, 0, SEEK_SET) == -ESPIPE);
	vfs_close(f);

	/* the heap has no leaks left from the failed paths above */
	CHECK(kalloc_check() == 0);
	TEST_DONE();
}
