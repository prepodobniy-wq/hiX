// SPDX-License-Identifier: GPL-2.0-only
/* qubix: the first device nodes and a few files to look at. */
#include <hix/kernel.h>
#include <hix/version.h>
#include <qubix/tty.h>
#include <qubix/vfs.h>

static ssize_t console_read(struct vnode *vn, off_t pos, char *buf, size_t len)
{
	(void)vn;
	(void)pos;
	return tty_read(buf, len);
}

static ssize_t console_write(struct vnode *vn, off_t pos, const char *buf,
			     size_t len)
{
	(void)vn;
	(void)pos;
	return tty_write(buf, len);
}

static ssize_t null_read(struct vnode *vn, off_t pos, char *buf, size_t len)
{
	(void)vn;
	(void)pos;
	(void)buf;
	(void)len;
	return 0;
}

static ssize_t null_write(struct vnode *vn, off_t pos, const char *buf,
			  size_t len)
{
	(void)vn;
	(void)pos;
	(void)buf;
	return (ssize_t)len;
}

static const struct vnode_ops console_ops = {
	.read = console_read,
	.write = console_write,
};

static const struct vnode_ops null_ops = {
	.read = null_read,
	.write = null_write,
};

static void write_file(const char *path, const char *text)
{
	struct file *f;

	if (vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC, &f) == 0) {
		vfs_write(f, text, strlen(text));
		vfs_close(f);
	}
}

void qubix_devices_init(void)
{
	vfs_mknod_chr("/dev/console", &console_ops);
	vfs_mknod_chr("/dev/null", &null_ops);

	write_file("/etc/motd",
		   "Welcome to hix - hi is not unix.\n"
		   "aether (tasks and ports), qubix (processes), steel (graphics).\n");
	write_file("/etc/hostname", "hix\n");
	write_file("/etc/release", HIX_NAME " " HIX_RELEASE "\n");
}
