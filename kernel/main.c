// SPDX-License-Identifier: GPL-2.0-only
/*
 * hix: kernel entry. boot.asm jumps here in 32-bit protected mode.
 *
 * Boot order: machine setup, memory, the aether layer (tasks,
 * scheduler, IPC), drivers, the qubix layer, then the first process.
 */
#define pr_fmt(fmt) "hix: " fmt

#include <hix/kernel.h>
#include <hix/driver.h>
#include <hix/multiboot.h>
#include <hix/version.h>
#include <asm/trap.h>
#include <aether/kalloc.h>
#include <aether/pmm.h>
#include <aether/sched.h>
#include <aether/vm.h>
#include <qubix/proc.h>
#include <qubix/syscall.h>

static void init_thread(void *arg)
{
	(void)arg;
	/* pid 1: from here on everything is a qubix process */
	if (proc_spawn("init") < 0)
		panic("cannot start init");
}

void hix_start(u32 magic, const struct multiboot_info *mbi)
{
	/* machine setup: descriptor tables, interrupts */
	gdt_init();
	arch_init_traps();
	idt_init();
	pic_init();

	/* consoles first, so everything below can report */
	driver_start_levels(DRV_LEVEL_CONSOLE, DRV_LEVEL_CONSOLE);
	pr_notice("%s %s (%s) - %s + %s + %s\n", HIX_NAME, HIX_RELEASE,
		  HIX_EXPAND, AETHER_NAME, QUBIX_NAME, STEEL_NAME);

	if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
		panic("not started by a Multiboot loader (magic %08x)", magic);

	/* aether: memory */
	pmm_init(mbi);
	vm_init();
	kalloc_init();

	/* aether: tasks, scheduler, ports */
	task_init();
	sched_init();

	/* drivers: timer, clock, keyboard */
	driver_start_levels(DRV_LEVEL_PLATFORM, DRV_LEVEL_LATE);

	/* qubix: processes and syscalls */
	qubix_init();

	if (!thread_create(kernel_task, "init", init_thread, NULL))
		panic("cannot create the init thread");

	pr_info("boot complete, starting the scheduler\n");
	sched_idle_loop();		/* the boot context becomes "idle" */
}
