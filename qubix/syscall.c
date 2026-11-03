// SPDX-License-Identifier: GPL-2.0-only
/*
 * qubix: system call dispatch. See include/qubix/syscall.h for the ABI.
 *
 * Arguments that are pointers are validated before use (user_ok): today
 * every caller is a kernel thread, but the checks are what keep this layer
 * honest once programs run in ring 3.
 */
#define pr_fmt(fmt) "qubix: " fmt

#include <hix/kernel.h>
#include <hix/driver.h>
#include <hix/rtc.h>
#include <asm/trap.h>
#include <aether/kalloc.h>
#include <aether/pmm.h>
#include <aether/sched.h>
#include <aether/vm.h>
#include <qubix/input.h>
#include <qubix/libq.h>
#include <qubix/proc.h>
#include <qubix/syscall.h>
#include <qubix/tty.h>
#include <steel/steel.h>
#include <steel/gfxdev.h>
#include <hix/version.h>

void steel_demo(void);

/* ---- argument checking ---------------------------------------------- */

static bool user_ok(const void *ptr, size_t len)
{
	unsigned long start = (unsigned long)ptr;
	unsigned long end = start + len;
	unsigned long page;

	if (start < PAGE_SIZE || end < start)
		return false;
	for (page = ALIGN_DOWN(start, PAGE_SIZE); page < end; page += PAGE_SIZE)
		if (!vm_addr_is_mapped(page))
			return false;
	return true;
}

/* copy a string argument into @dst; returns 0 or -errno */
static int copy_str(char *dst, const char *src, size_t size)
{
	size_t i;

	if (!user_ok(src, 1))
		return -EFAULT;
	for (i = 0; i < size; i++) {
		if (!user_ok(src + i, 1))
			return -EFAULT;
		dst[i] = src[i];
		if (!src[i])
			return 0;
	}
	return -ENAMETOOLONG;
}

/* ---- sysctl: values are text, like `sysctl -a` ----------------------- */

static int fmt_ps(char *buf, size_t size)
{
	struct task_info *tasks = kmalloc(sizeof(*tasks) * 32);
	struct thread_info *threads = kmalloc(sizeof(*threads) * 64);
	int nt, nth, i, len = 0;

	if (!tasks || !threads) {
		kfree(tasks);
		kfree(threads);
		return -ENOMEM;
	}
	nt = task_snapshot(tasks, 32);
	nth = thread_snapshot(threads, 64);

	len += snprintf(buf + len, size - len,
			"TASK  NAME             THREADS PORTS\n");
	for (i = 0; i < nt && (size_t)len < size; i++)
		len += snprintf(buf + len, size - len, "%-5d %-16s %-7d %u\n",
				tasks[i].id, tasks[i].name, tasks[i].nthreads,
				tasks[i].nports);
	len += snprintf(buf + len, size - len,
			"\nTID   TASK  NAME             STATE    CPU(ticks)\n");
	for (i = 0; i < nth && (size_t)len < size; i++)
		len += snprintf(buf + len, size - len,
				"%-5d %-5d %-16s %-8s %lu\n", threads[i].tid,
				threads[i].task_id, threads[i].name,
				thread_state_name(threads[i].state),
				threads[i].cpu_ticks);
	kfree(tasks);
	kfree(threads);
	return len;
}

static int fmt_procs(char *buf, size_t size)
{
	struct proc_info *p = kmalloc(sizeof(*p) * 32);
	int n, i, len = 0;

	if (!p)
		return -ENOMEM;
	n = proc_snapshot(p, 32);
	len += snprintf(buf, size, "PID   PPID  STATE    COMMAND\n");
	for (i = 0; i < n && (size_t)len < size; i++)
		len += snprintf(buf + len, size - len, "%-5d %-5d %-8s %s\n",
				p[i].pid, p[i].ppid,
				p[i].state == PROC_ZOMBIE ? "zombie" : "running",
				p[i].comm);
	kfree(p);
	return len;
}

static int fmt_ioreg(char *buf, size_t size)
{
	const struct hix_driver *d;
	unsigned int i;
	int len = 0;

	len += snprintf(buf, size, "+-o hix-platform\n");
	for (i = 0; (d = driver_get(i)) != NULL && (size_t)len < size; i++)
		len += snprintf(buf + len, size - len,
				"  +-o %-10s <class %-8s level %d  %s>\n",
				d->name, d->class, d->level,
				driver_state_name(d->state));
	return len;
}

static int fmt_irqs(char *buf, size_t size)
{
	int irq, len = 0;

	for (irq = 0; irq < NR_IRQS && (size_t)len < size; irq++) {
		unsigned long count;
		const char *name = irq_name(irq, &count);

		if (name)
			len += snprintf(buf + len, size - len,
					"%2d: %10lu  %s\n", irq, count, name);
	}
	return len;
}

static int fmt_heap(char *buf, size_t size)
{
	struct kalloc_stats st;

	kalloc_get_stats(&st);
	return snprintf(buf, size,
			"heap %lu KiB, used %lu KiB in %lu blocks, "
			"%lu free blocks (largest %lu KiB), %d bad\n",
			(unsigned long)st.total >> 10,
			(unsigned long)st.used >> 10,
			(unsigned long)st.blocks_used,
			(unsigned long)st.blocks_free,
			(unsigned long)st.largest_free >> 10, kalloc_check());
}

static int sysctl_text(const char *name, char *buf, size_t size)
{
	if (!strcmp(name, "kern.ostype"))
		return snprintf(buf, size, "%s", HIX_NAME);
	if (!strcmp(name, "kern.osrelease"))
		return snprintf(buf, size, "%s", HIX_RELEASE);
	if (!strcmp(name, "kern.version"))
		return snprintf(buf, size, "%s %s (%s): %s + %s + %s",
				HIX_NAME, HIX_RELEASE, HIX_EXPAND,
				AETHER_NAME, QUBIX_NAME, STEEL_NAME);
	if (!strcmp(name, "kern.hostname"))
		return snprintf(buf, size, "hix");
	if (!strcmp(name, "kern.uptime"))
		return snprintf(buf, size, "%lu", jiffies / HZ);
	if (!strcmp(name, "hw.ncpu"))
		return snprintf(buf, size, "1");
	if (!strcmp(name, "hw.pagesize"))
		return snprintf(buf, size, "%lu", PAGE_SIZE);
	if (!strcmp(name, "hw.memsize"))
		return snprintf(buf, size, "%lu",
				pmm_total_count() * PAGE_SIZE);
	if (!strcmp(name, "hw.freemem"))
		return snprintf(buf, size, "%lu", pmm_free_count() * PAGE_SIZE);
	if (!strcmp(name, "hix.ps"))
		return fmt_ps(buf, size);
	if (!strcmp(name, "hix.procs"))
		return fmt_procs(buf, size);
	if (!strcmp(name, "hix.devtree"))
		return fmt_ioreg(buf, size);
	if (!strcmp(name, "hix.irqs"))
		return fmt_irqs(buf, size);
	if (!strcmp(name, "hix.heap"))
		return fmt_heap(buf, size);
	if (!strcmp(name, "hix.dmesg"))
		return (int)printk_log_read(buf, size);
	return -ENOENT;
}

static long sys_ioctl(int fd, long req, long arg)
{
	if (fd < 0 || fd > 2)
		return -ENOTTY;
	switch (req) {
	case TTY_SETRAW:
		tty_set_raw(arg != 0);
		return 0;
	case TTY_SETTIMEOUT:
		tty_set_timeout(arg > 0 ? (unsigned long)arg : 0);
		return 0;
	default:
		return -EINVAL;
	}
}

static long sys_sysctlbyname(const char *uname, char *ubuf, size_t len)
{
	char name[32];
	char *tmp;
	int n, err;

	err = copy_str(name, uname, sizeof(name));
	if (err)
		return err;
	if (!len || len > 16384 || !user_ok(ubuf, len))
		return -EFAULT;
	tmp = kmalloc(len);
	if (!tmp)
		return -ENOMEM;
	n = sysctl_text(name, tmp, len);
	if (n >= 0) {
		if ((size_t)n >= len)
			n = (int)len - 1;
		memcpy(ubuf, tmp, (size_t)n);
		ubuf[n] = '\0';
	}
	kfree(tmp);
	return n;
}

/* ---- process calls ------------------------------------------------------- */

static long sys_open(const char *upath, int flags)
{
	struct proc *p = proc_current();
	char path[VFS_PATH_MAX];
	struct file *f;
	int err, fd;

	err = copy_str(path, upath, sizeof(path));
	if (err)
		return err;
	err = vfs_open(path, flags, &f);
	if (err)
		return err;
	fd = proc_fd_alloc(p, f);
	if (fd < 0)
		vfs_close(f);
	return fd;
}

static long sys_read(int fd, char *buf, size_t len)
{
	struct file *f = proc_fd_get(proc_current(), fd);

	if (!f)
		return -EBADF;
	if (len && !user_ok(buf, len))
		return -EFAULT;
	return vfs_read(f, buf, len);
}

static long sys_write(int fd, const char *buf, size_t len)
{
	struct file *f = proc_fd_get(proc_current(), fd);

	if (!f)
		return -EBADF;
	if (len && !user_ok(buf, len))
		return -EFAULT;
	return vfs_write(f, buf, len);
}

static long sys_path_op(const char *upath, int (*op)(const char *))
{
	char path[VFS_PATH_MAX];
	int err = copy_str(path, upath, sizeof(path));

	return err ? err : op(path);
}

static long sys_getdirentries(const char *upath, struct dirent_rec *out,
			      int max)
{
	char path[VFS_PATH_MAX];
	struct dirent_rec *tmp;
	int n, err;

	err = copy_str(path, upath, sizeof(path));
	if (err)
		return err;
	if (max <= 0 || max > 64 || !user_ok(out, sizeof(*out) * max))
		return -EFAULT;
	tmp = kmalloc(sizeof(*tmp) * max);
	if (!tmp)
		return -ENOMEM;
	n = vfs_readdir(path, tmp, max);
	if (n > 0)
		memcpy(out, tmp, sizeof(*tmp) * n);
	kfree(tmp);
	return n;
}

static long sys_reboot(int howto)
{
	if (howto & RB_PANIC)
		panic("panic requested from userland");
	if (howto & RB_HALT) {
		pr_notice("system halted, it is now safe to power off\n");
		arch_poweroff();
		arch_halt();
	}
	pr_notice("rebooting\n");
	arch_reboot();
}

static long sys_gettimeofday(struct timeval_hix *tv)
{
	struct rtc_time tm;

	if (!user_ok(tv, sizeof(*tv)))
		return -EFAULT;
	if (rtc_read(&tm))
		return -ENODEV;
	tv->tv_sec = (long)rtc_mktime(&tm);
	tv->tv_usec = 0;
	return 0;
}

static long sys_spawn(const char *ucmd)
{
	char cmd[VFS_PATH_MAX];
	int err = copy_str(cmd, ucmd, sizeof(cmd));

	return err ? err : proc_spawn(cmd);
}

static long sys_wait4(int pid, int *status)
{
	int st = 0;
	long ret;

	if (status && !user_ok(status, sizeof(*status)))
		return -EFAULT;
	ret = proc_wait(pid, &st);
	if (ret > 0 && status)
		*status = st;
	return ret;
}

/* ---- port traps ------------------------------------------------------ */

static long ae_msg_trap(struct ae_msg *msg, u32 option,
			  ae_port_name_t rcv_name, unsigned long timeout)
{
	if (!user_ok(msg, sizeof(*msg)))
		return AE_INVALID_ADDRESS;
	return ae_msg(thread_current()->task, msg, option, rcv_name,
			timeout);
}

static long trap_bootstrap_register(const char *uname, ae_port_name_t port)
{
	char name[32];

	if (copy_str(name, uname, sizeof(name)))
		return AE_INVALID_ADDRESS;
	return bootstrap_register(thread_current()->task, name, port);
}

static long trap_bootstrap_look_up(const char *uname, ae_port_name_t *out)
{
	char name[32];
	ae_port_name_t port;
	ae_ret_t kr;

	if (copy_str(name, uname, sizeof(name)) ||
	    !user_ok(out, sizeof(*out)))
		return AE_INVALID_ADDRESS;
	kr = bootstrap_look_up(thread_current()->task, name, &port);
	if (kr == AE_OK)
		*out = port;
	return kr;
}

static long trap_reply_port(void)
{
	ae_port_name_t name = AE_PORT_NULL;

	if (ae_port_allocate(thread_current()->task, &name))
		return AE_PORT_NULL;
	return (long)name;
}

/* ---- steel control ----------------------------------------------------- */

static struct proc *display_owner;

/* also called from proc_exit(): a program that dies gives the screen back */
void qubix_display_release(struct proc *p)
{
	unsigned long flags;

	local_irq_save(flags);
	if (display_owner != p) {
		local_irq_restore(flags);
		return;
	}
	display_owner = NULL;
	local_irq_restore(flags);
	input_release();
	if (steel_gfx_active()) {
		steel_gfx_leave();
		input_set_geometry(640, 400, 8, 16, 0, 0, false);
	}
	steel_con_set_suspended(false);
}

static long sys_steelctl(long op, long a, long b, long c)
{
	struct proc *me = proc_current();
	struct steel_device *dev = steel_get_device();
	long ret;

	if (op == STEEL_CTL_DEMO) {
		steel_demo();
		return 0;
	}
	if (op == STEEL_CTL_MSEC)
		return (long)(clock_uptime_ms() & 0x7FFFFFFF);
	if (!dev)
		return -ENODEV;

	if (op == STEEL_CTL_GRAB) {
		ret = input_grab();
		if (ret)
			return ret;
		display_owner = me;
		steel_con_set_suspended(true);
		if (a == STEEL_GRAB_PIXELS && !steel_gfx_enter()) {
			struct pixdev *pd = steel_gfx_pixdev();

			input_set_geometry(GFX_SCREEN_W, GFX_SCREEN_H, PIX_CELL_W,
					   PIX_CELL_H, pd->off_x, pd->off_y, true);
			return 1;
		}
		return 0;
	}
	if (display_owner != me)
		return -EPERM;

	switch (op) {
	case STEEL_CTL_UNGRAB:
		qubix_display_release(me);
		return 0;
	case STEEL_CTL_PRESENT: {
		struct steel_surface s;

		if (b != dev->w || c != dev->h)
			return -EINVAL;
		if (!user_ok((const void *)a, (size_t)(b * c) * sizeof(steel_cell_t)))
			return -EFAULT;
		s.w = (int)b;
		s.h = (int)c;
		s.cells = (steel_cell_t *)a;
		s.owns_cells = false;
		steel_present(&s);
		return 0;
	}
	case STEEL_CTL_EVENT: {
		struct input_event ev;

		if (!user_ok((const void *)a, sizeof(ev)))
			return -EFAULT;
		ret = input_wait(&ev, b);
		if (!ret) {
			struct pixdev *pd = steel_gfx_pixdev();

			if (pd && ev.type == INPUT_MOUSE)
				pixdev_cursor(pd, ev.px, ev.py, true);
			memcpy((void *)a, &ev, sizeof(ev));
		}
		return ret;
	}
	case STEEL_CTL_GFXINFO: {
		struct pixdev *pd = steel_gfx_pixdev();
		int *out = (int *)a;

		if (!pd)
			return -ENODEV;
		if (!user_ok(out, 6 * sizeof(int)))
			return -EFAULT;
		out[0] = pd->fb.w;
		out[1] = pd->fb.h;
		out[2] = PIX_CELL_W;
		out[3] = PIX_CELL_H;
		out[4] = pd->off_x;
		out[5] = pd->off_y;
		return 0;
	}
	case STEEL_CTL_CLIENT_SET:
	case STEEL_CTL_CLIENT_DIRTY: {
		struct pixdev *pd = steel_gfx_pixdev();
		const struct steel_client_req *rq = (const struct steel_client_req *)a;

		if (!pd)
			return -ENODEV;
		if (!user_ok(rq, sizeof(*rq)))
			return -EFAULT;
		if (op == STEEL_CTL_CLIENT_DIRTY) {
			pixdev_client_dirty(pd, rq->idx, rq->x, rq->y, rq->w, rq->h);
			return 0;
		}
		if (rq->w <= 0 || rq->h <= 0 || rq->stride < rq->w ||
		    rq->w > 4096 || rq->h > 4096 ||
		    !user_ok((const void *)rq->px,
			     (size_t)rq->stride * (size_t)rq->h * sizeof(u32)))
			return -EFAULT;
		return pixdev_client_set(pd, rq->idx, rq->x, rq->y, rq->w, rq->h,
					 (const u32 *)rq->px, rq->stride);
	}
	case STEEL_CTL_CLIENT_CLEAR: {
		struct pixdev *pd = steel_gfx_pixdev();

		if (!pd)
			return -ENODEV;
		pixdev_client_clear(pd, (int)a);
		return 0;
	}
	case STEEL_CTL_INFO: {
		int *out = (int *)a;

		if (!user_ok(out, 2 * sizeof(int)))
			return -EFAULT;
		out[0] = dev->w;
		out[1] = dev->h;
		return 0;
	}
	default:
		return -EINVAL;
	}
}

/* ---- the dispatcher --------------------------------------------------- */

void qubix_syscall(struct pt_regs *r)
{
	long a1 = (long)r->ebx, a2 = (long)r->ecx, a3 = (long)r->edx;
	long a4 = (long)r->esi;
	long ret;

	switch ((int)r->eax) {
	case SYS_exit:
		proc_exit((int)a1);
	case SYS_read:
		ret = sys_read((int)a1, (char *)a2, (size_t)a3);
		break;
	case SYS_write:
		ret = sys_write((int)a1, (const char *)a2, (size_t)a3);
		break;
	case SYS_open:
		ret = sys_open((const char *)a1, (int)a2);
		break;
	case SYS_close:
		ret = proc_fd_close(proc_current(), (int)a1);
		break;
	case SYS_wait4:
		ret = sys_wait4((int)a1, (int *)a2);
		break;
	case SYS_unlink:
		ret = sys_path_op((const char *)a1, vfs_unlink);
		break;
	case SYS_mkdir:
		ret = sys_path_op((const char *)a1, vfs_mkdir);
		break;
	case SYS_getpid:
		ret = proc_current()->pid;
		break;
	case SYS_getppid:
		ret = proc_current()->ppid;
		break;
	case SYS_ioctl:
		ret = sys_ioctl((int)a1, a2, a3);
		break;
	case SYS_reboot:
		ret = sys_reboot((int)a1);
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((struct timeval_hix *)a1);
		break;
	case SYS_getdirentries:
		ret = sys_getdirentries((const char *)a1,
					(struct dirent_rec *)a2, (int)a3);
		break;
	case SYS_lseek: {
		struct file *f = proc_fd_get(proc_current(), (int)a1);

		ret = f ? vfs_lseek(f, (off_t)a2, (int)a3) : -EBADF;
		break;
	}
	case SYS_posix_spawn:
		ret = sys_spawn((const char *)a1);
		break;
	case SYS_sysctlbyname:
		ret = sys_sysctlbyname((const char *)a1, (char *)a2,
				       (size_t)a3);
		break;
	case SYS_steelctl:
		ret = sys_steelctl(a1, a2, a3, a4);
		break;

	case AE_reply_port:
		ret = trap_reply_port();
		break;
	case AE_thread_self:
		ret = thread_current()->tid;
		break;
	case AE_task_self:
		ret = thread_current()->task->id;
		break;
	case AE_msg_trap:
		ret = ae_msg_trap((struct ae_msg *)a1, (u32)a2,
				    (ae_port_name_t)a3, (unsigned long)a4);
		break;
	case AE_swtch:
		thread_yield();
		ret = 0;
		break;
	case AE_clock_sleep:
		thread_sleep(msecs_to_jiffies((unsigned long)a1));
		ret = 0;
		break;
	case AE_bootstrap_register:
		ret = trap_bootstrap_register((const char *)a1,
					      (ae_port_name_t)a2);
		break;
	case AE_bootstrap_look_up:
		ret = trap_bootstrap_look_up((const char *)a1,
					     (ae_port_name_t *)a2);
		break;
	case AE_port_deallocate:
		ret = ae_port_deallocate(thread_current()->task,
					   (ae_port_name_t)a1);
		break;
	case AE_port_insert_send:
		ret = ae_port_insert_send(thread_current()->task,
					    (ae_port_name_t)a1);
		break;
	default:
		ret = -ENOSYS;
		break;
	}
	r->eax = (u32)ret;
}

void qubix_init(void)
{
	extern void qubix_devices_init(void);

	vfs_init();
	proc_init();
	qubix_devices_init();
	set_syscall_handler(qubix_syscall);
	pr_info("qubix up: vfs, processes, %d programs\n", qprogs_count);
}
