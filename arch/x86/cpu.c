// SPDX-License-Identifier: GPL-2.0-only
/* CPU helpers: control registers, register dumps, reboot and power off. */
#define pr_fmt(fmt) fmt

#include <hix/kernel.h>
#include <asm/io.h>
#include <asm/trap.h>
#include <asm/irqflags.h>
#include <aether/vm.h>

unsigned long read_cr0(void)
{
	unsigned long v;

	__asm__ volatile("mov %%cr0, %0" : "=r"(v));
	return v;
}

unsigned long read_cr2(void)
{
	unsigned long v;

	__asm__ volatile("mov %%cr2, %0" : "=r"(v));
	return v;
}

unsigned long read_cr3(void)
{
	unsigned long v;

	__asm__ volatile("mov %%cr3, %0" : "=r"(v));
	return v;
}

unsigned long read_cr4(void)
{
	unsigned long v;

	__asm__ volatile("mov %%cr4, %0" : "=r"(v));
	return v;
}

void write_cr0(unsigned long v)
{
	__asm__ volatile("mov %0, %%cr0" : : "r"(v) : "memory");
}

void write_cr3(unsigned long v)
{
	__asm__ volatile("mov %0, %%cr3" : : "r"(v) : "memory");
}

void write_cr4(unsigned long v)
{
	__asm__ volatile("mov %0, %%cr4" : : "r"(v) : "memory");
}

void invlpg(unsigned long addr)
{
	__asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

void dump_regs(const struct pt_regs *r)
{
	pr_emerg("EIP: %04x:%08x EFLAGS: %08x\n", r->cs, r->eip, r->eflags);
	pr_emerg("EAX: %08x EBX: %08x ECX: %08x EDX: %08x\n",
		 r->eax, r->ebx, r->ecx, r->edx);
	pr_emerg("ESI: %08x EDI: %08x EBP: %08x ESP: %08x\n",
		 r->esi, r->edi, r->ebp, (u32)(uintptr_t)&r->user_esp);
	pr_emerg(" DS: %04x  ES: %04x  FS: %04x  GS: %04x\n",
		 r->ds, r->es, r->fs, r->gs);
	pr_emerg("CR0: %08x CR2: %08x CR3: %08x CR4: %08x\n",
		 (u32)read_cr0(), (u32)read_cr2(), (u32)read_cr3(),
		 (u32)read_cr4());
}

/*
 * Walk the %ebp chain. Every function is built with frame pointers, so each
 * frame is { saved %ebp, return address }. Stop at anything suspicious: a
 * panic must not fault again while reporting a fault.
 */
void dump_stack_trace(unsigned long ebp)
{
	int depth;

	pr_emerg("Call trace:\n");
	for (depth = 0; depth < 16; depth++) {
		unsigned long next, ret;

		if (ebp < 0x100000 || (ebp & 3) || ebp > 0x40000000UL)
			break;
		if (!vm_addr_is_mapped(ebp) || !vm_addr_is_mapped(ebp + 4))
			break;
		next = ((unsigned long *)ebp)[0];
		ret = ((unsigned long *)ebp)[1];
		if (!ret)
			break;
		pr_emerg("  [<%08x>]\n", (u32)ret);
		if (next <= ebp)
			break;
		ebp = next;
	}
}

void arch_halt(void)
{
	for (;;)
		__asm__ volatile("cli; hlt");
}

void arch_reboot(void)
{
	int i;

	local_irq_disable();

	/* 1: pulse the CPU reset line through the keyboard controller */
	for (i = 0; i < 0x10000 && (inb(0x64) & 2); i++)
		io_wait();
	outb(0x64, 0xFE);
	for (i = 0; i < 0x10000; i++)
		io_wait();

	/* 2: still here? load an empty IDT and fault -> triple fault */
	{
		static const struct {
			u16 limit;
			u32 base;
		} __packed null_idt = { 0, 0 };

		__asm__ volatile("lidt %0; int3" : : "m"(null_idt));
	}
	arch_halt();
}

/* works on QEMU, Bochs and VirtualBox; real hardware needs ACPI tables */
void arch_poweroff(void)
{
	outw(0x604, 0x2000);	/* QEMU (q35/piix4 ACPI) */
	outw(0xB004, 0x2000);	/* Bochs, old QEMU */
	outw(0x4004, 0x3400);	/* VirtualBox */
}
