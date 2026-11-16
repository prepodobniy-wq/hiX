// SPDX-License-Identifier: GPL-2.0-only
/*
 * aether: the CPU side of ring 3: page directory and kernel stack follow the
 * running thread, and a kernel thread can turn itself into a user process.
 */
#define pr_fmt(fmt) "aether: uspace: " fmt

#include <hix/kernel.h>
#include <asm/trap.h>
#include <aether/kalloc.h>
#include <aether/sched.h>
#include <aether/uspace.h>
#include <aether/vm.h>

/* schedule() calls this with interrupts off, right before the stack switch */
void arch_thread_switch(struct thread *next)
{
	unsigned long cr3;

	if (!vm_enabled())
		return;
	if (next->stack)
		gdt_set_kernel_stack((unsigned long)next->stack +
				     next->stack_pages * PAGE_SIZE);
	cr3 = next->uspace ? (unsigned long)next->uspace->pgd :
			     (unsigned long)vm_kernel_pgdir();
	if (cr3 != read_cr3())
		write_cr3(cr3);
}

static int exec_image(const void *img, size_t len, bool free_img, int argc,
		      char **argv)
{
	struct thread *t = thread_current();
	struct uspace *us;
	unsigned long entry, sp;
	int ret;

	if (!t || !vm_enabled() || t->uspace)
		ret = -EINVAL;
	else if (!(us = kzalloc(sizeof(*us))))
		ret = -ENOMEM;
	else
		ret = 0;
	if (ret) {
		if (free_img)
			kfree((void *)img);
		return ret;
	}
	ret = uspace_create(us, vm_kernel_pgdir());
	if (!ret)
		ret = uspace_load_elf(us, img, len, &entry, &sp);
	if (!ret)
		ret = uspace_push_args(us, &sp, argc, argv);
	if (free_img)
		kfree((void *)img);	/* the pages hold the program now */
	if (ret) {
		uspace_destroy(us);
		kfree(us);
		return ret;
	}

	local_irq_disable();		/* iret turns interrupts back on */
	t->uspace = us;
	arch_thread_switch(t);		/* esp0 and CR3 for this very thread */
	enter_user(entry, sp);
}

int uspace_exec(const void *img, size_t len)
{
	return exec_image(img, len, false, 0, NULL);
}

int uspace_exec_free(void *img, size_t len)
{
	return exec_image(img, len, true, 0, NULL);
}

int uspace_exec_args(void *img, size_t len, int argc, char **argv)
{
	return exec_image(img, len, true, argc, argv);
}

void uspace_release(struct thread *t)
{
	struct uspace *us = t->uspace;
	unsigned long flags;

	if (!us)
		return;
	local_irq_save(flags);
	t->uspace = NULL;
	arch_thread_switch(t);		/* back to the kernel directory */
	local_irq_restore(flags);
	uspace_destroy(us);
	kfree(us);
}
