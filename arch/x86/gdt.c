// SPDX-License-Identifier: GPL-2.0-only
/*
 * Global Descriptor Table and Task State Segments.
 *
 * Flat 4 GiB segments for ring 0 and ring 3 (ring 3 is not used yet), plus
 * two TSSs: the main one (required by the CPU for task switches) and a
 * dedicated one for the double fault handler, which gives it a private stack.
 * That is the only way to report a kernel stack overflow instead of ending
 * in a silent triple fault.
 */
#define pr_fmt(fmt) "gdt: " fmt

#include <hix/kernel.h>
#include <asm/trap.h>

struct gdt_entry {
	u16 limit_low;
	u16 base_low;
	u8  base_mid;
	u8  access;
	u8  granularity;	/* flags (high nibble) + limit 19:16 */
	u8  base_high;
} __packed;

struct desc_ptr {
	u16 limit;
	u32 base;
} __packed;

struct tss {
	u32 link;
	u32 esp0, ss0, esp1, ss1, esp2, ss2;
	u32 cr3, eip, eflags;
	u32 eax, ecx, edx, ebx, esp, ebp, esi, edi;
	u32 es, cs, ss, ds, fs, gs;
	u32 ldt;
	u32 iomap;		/* trap flag (low 16), iomap base (high 16) */
} __packed;

_Static_assert(sizeof(struct gdt_entry) == 8, "GDT entry must be 8 bytes");
_Static_assert(sizeof(struct desc_ptr) == 6, "GDTR must be 6 bytes");
_Static_assert(sizeof(struct tss) == 104, "32-bit TSS must be 104 bytes");

#define GDT_NULL	0
#define GDT_KCODE	1
#define GDT_KDATA	2
#define GDT_UCODE	3
#define GDT_UDATA	4
#define GDT_TSS		5
#define GDT_TSS_DF	6
#define GDT_ENTRIES	7

#define SEL_TSS		(GDT_TSS * 8)
#define SEL_TSS_DF	(GDT_TSS_DF * 8)

#define DF_STACK_SIZE	4096

static struct gdt_entry gdt[GDT_ENTRIES];
static struct desc_ptr gdtr;

static struct tss main_tss;
static struct tss df_tss;
static u8 df_stack[DF_STACK_SIZE] __aligned(16);

extern void gdt_flush(const struct desc_ptr *gdtr);
extern void tss_load(u16 selector);

static void gdt_set(int idx, u32 base, u32 limit, u8 access, u8 flags)
{
	gdt[idx].limit_low = limit & 0xFFFF;
	gdt[idx].base_low = base & 0xFFFF;
	gdt[idx].base_mid = (base >> 16) & 0xFF;
	gdt[idx].access = access;
	gdt[idx].granularity = ((limit >> 16) & 0x0F) | (flags & 0xF0);
	gdt[idx].base_high = (base >> 24) & 0xFF;
}

/*
 * Runs on its own stack, entered through the task gate in IDT[8]. The CPU
 * stored the register state of the faulting context in main_tss.
 */
static void double_fault_task(void)
{
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));
	regs.eip = main_tss.eip;
	regs.eflags = main_tss.eflags;
	regs.cs = main_tss.cs;
	regs.ds = main_tss.ds;
	regs.es = main_tss.es;
	regs.fs = main_tss.fs;
	regs.gs = main_tss.gs;
	regs.eax = main_tss.eax;
	regs.ecx = main_tss.ecx;
	regs.edx = main_tss.edx;
	regs.ebx = main_tss.ebx;
	regs.esp_dummy = main_tss.esp;	/* the real %esp at fault time */
	regs.ebp = main_tss.ebp;
	regs.esi = main_tss.esi;
	regs.edi = main_tss.edi;
	regs.vector = 8;

	double_fault_report(&regs);	/* never returns */
}

void gdt_init(void)
{
	gdt_set(GDT_NULL, 0, 0, 0, 0);
	gdt_set(GDT_KCODE, 0, 0xFFFFF, 0x9A, 0xC0);	/* ring 0 code */
	gdt_set(GDT_KDATA, 0, 0xFFFFF, 0x92, 0xC0);	/* ring 0 data */
	gdt_set(GDT_UCODE, 0, 0xFFFFF, 0xFA, 0xC0);	/* ring 3 code */
	gdt_set(GDT_UDATA, 0, 0xFFFFF, 0xF2, 0xC0);	/* ring 3 data */

	memset(&main_tss, 0, sizeof(main_tss));
	main_tss.ss0 = KERNEL_DS;
	main_tss.iomap = sizeof(main_tss) << 16;

	memset(&df_tss, 0, sizeof(df_tss));
	df_tss.eip = (u32)(uintptr_t)double_fault_task;
	df_tss.esp = (u32)(uintptr_t)df_stack + DF_STACK_SIZE;
	df_tss.ebp = 0;
	df_tss.cs = KERNEL_CS;
	df_tss.ss = df_tss.ds = df_tss.es = KERNEL_DS;
	df_tss.fs = df_tss.gs = KERNEL_DS;
	df_tss.eflags = 0x2;			/* interrupts off */
	df_tss.iomap = sizeof(df_tss) << 16;

	/* available 32-bit TSS, byte granularity */
	gdt_set(GDT_TSS, (u32)(uintptr_t)&main_tss, sizeof(main_tss) - 1,
		0x89, 0x00);
	gdt_set(GDT_TSS_DF, (u32)(uintptr_t)&df_tss, sizeof(df_tss) - 1,
		0x89, 0x00);

	gdtr.limit = sizeof(gdt) - 1;
	gdtr.base = (u32)(uintptr_t)gdt;
	gdt_flush(&gdtr);
	tss_load(SEL_TSS);
}

/* the double fault task needs the kernel page directory once paging is on */
void gdt_set_df_cr3(unsigned long cr3)
{
	df_tss.cr3 = cr3;
}

/* kernel stack for the next privilege change (used once ring 3 exists) */
void gdt_set_kernel_stack(unsigned long esp0)
{
	main_tss.esp0 = esp0;
}

u16 gdt_df_selector(void)
{
	return SEL_TSS_DF;
}
