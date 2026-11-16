// SPDX-License-Identifier: GPL-2.0-only
/*
 * qubix: ring 3 programs. A user program is an ELF image; its process is an
 * ordinary qubix process whose first thread turns itself into user mode.
 * Faults in user mode end the process, never the kernel.
 */
#define pr_fmt(fmt) "qubix: " fmt

#include <hix/kernel.h>
#include <asm/trap.h>
#include <aether/uspace.h>
#include <qubix/proc.h>
#include <aether/kalloc.h>
#include <qubix/initfs.h>
#include <qubix/uexec.h>
#include <qubix/vfs.h>

#define FAULT_STATUS	139		/* 128 + SIGSEGV, like a shell would show */

void arch_user_fault(const struct pt_regs *regs, const char *what)
{
	struct proc *p = proc_current();

	local_irq_enable();
	pr_warn("pid %d (%s) killed: %s at eip %08x\n", (int)p->pid, p->comm,
		what, regs->eip);
	proc_exit(FAULT_STATUS);
}

/* the programs linked into the kernel image (arch/x86/ublob.asm) */
extern const char user_hello_start[], user_hello_end[];
extern const char user_fault_start[], user_fault_end[];
extern const char user_probe_start[], user_probe_end[];

static int run_blob(const char *start, const char *end)
{
	return -uspace_exec(start, (size_t)(end - start));
}

int uexec_hello(int argc, char **argv)
{
	return run_blob(user_hello_start, user_hello_end);
}

int uexec_fault(int argc, char **argv)
{
	return run_blob(user_fault_start, user_fault_end);
}

int uexec_probe(int argc, char **argv)
{
	return run_blob(user_probe_start, user_probe_end);
}

/* ---- programs that live in /bin ----------------------------------------- */

static void bin_path(char *path, size_t size, const char *name)
{
	snprintf(path, size, "/bin/%s", name);
}

bool uexec_exists(const char *name)
{
	char path[VFS_PATH_MAX];
	struct file *f;

	if (!name[0] || strchr(name, '/') || strlen(name) > VFS_NAME_MAX)
		return false;
	bin_path(path, sizeof(path), name);
	if (vfs_open(path, O_RDONLY, &f))
		return false;
	vfs_close(f);
	return true;
}

#define UEXEC_MAX	(256 * 1024)

/* read /bin/@name and run it in ring 3; only returns on failure (-errno) */
int uexec_file(const char *name, int argc, char **argv)
{
	char path[VFS_PATH_MAX];
	struct file *f;
	size_t size, done = 0;
	u8 *img;

	bin_path(path, sizeof(path), name);
	if (vfs_open(path, O_RDONLY, &f))
		return -ENOENT;
	size = (size_t)vfs_lseek(f, 0, 2);
	vfs_lseek(f, 0, 0);
	img = size && size <= UEXEC_MAX ? kmalloc(size) : NULL;
	while (img && done < size) {
		ssize_t n = vfs_read(f, (char *)img + done, size - done);

		if (n <= 0)
			break;
		done += (size_t)n;
	}
	vfs_close(f);
	if (!img)
		return size ? -ENOMEM : -ENOEXEC;
	if (done != size) {
		kfree(img);
		return -EIO;
	}
	return uspace_exec_args(img, size, argc, argv);
}
