// SPDX-License-Identifier: GPL-2.0-only
/* Legacy 8259A programmable interrupt controller pair. */

#include <hix/kernel.h>
#include <asm/io.h>
#include <asm/trap.h>

#define PIC1_CMD	0x20
#define PIC1_DATA	0x21
#define PIC2_CMD	0xA0
#define PIC2_DATA	0xA1

#define PIC_EOI		0x20
#define PIC_READ_ISR	0x0B

static u8 cached_mask[2] = { 0xFF, 0xFF };

void pic_init(void)
{
	outb(PIC1_CMD, 0x11);		/* ICW1: init, expect ICW4 */
	io_wait();
	outb(PIC2_CMD, 0x11);
	io_wait();
	outb(PIC1_DATA, IRQ0_VECTOR);	/* ICW2: vector offsets */
	io_wait();
	outb(PIC2_DATA, IRQ0_VECTOR + 8);
	io_wait();
	outb(PIC1_DATA, 0x04);		/* ICW3: slave on IRQ2 */
	io_wait();
	outb(PIC2_DATA, 0x02);		/* ICW3: slave id */
	io_wait();
	outb(PIC1_DATA, 0x01);		/* ICW4: 8086 mode */
	io_wait();
	outb(PIC2_DATA, 0x01);
	io_wait();

	/* everything masked except the cascade line */
	cached_mask[0] = 0xFB;
	cached_mask[1] = 0xFF;
	outb(PIC1_DATA, cached_mask[0]);
	outb(PIC2_DATA, cached_mask[1]);
}

void pic_mask(int irq)
{
	if (irq < 0 || irq >= NR_IRQS || irq == 2)
		return;
	if (irq < 8) {
		cached_mask[0] |= 1 << irq;
		outb(PIC1_DATA, cached_mask[0]);
	} else {
		cached_mask[1] |= 1 << (irq - 8);
		outb(PIC2_DATA, cached_mask[1]);
	}
}

void pic_unmask(int irq)
{
	if (irq < 0 || irq >= NR_IRQS)
		return;
	if (irq < 8) {
		cached_mask[0] &= ~(1 << irq);
		outb(PIC1_DATA, cached_mask[0]);
	} else {
		cached_mask[1] &= ~(1 << (irq - 8));
		outb(PIC2_DATA, cached_mask[1]);
	}
}

void pic_eoi(int irq)
{
	if (irq >= 8)
		outb(PIC2_CMD, PIC_EOI);
	outb(PIC1_CMD, PIC_EOI);
}

/*
 * IRQ 7 and 15 can fire spuriously. A real one has its bit set in the
 * In-Service Register. A spurious IRQ 15 still needs an EOI for the master
 * (the cascade line was really asserted), a spurious IRQ 7 needs none.
 */
bool pic_irq_is_spurious(int irq)
{
	u8 isr;

	if (irq == 7) {
		outb(PIC1_CMD, PIC_READ_ISR);
		isr = inb(PIC1_CMD);
		return !(isr & 0x80);
	}
	if (irq == 15) {
		outb(PIC2_CMD, PIC_READ_ISR);
		isr = inb(PIC2_CMD);
		if (!(isr & 0x80)) {
			outb(PIC1_CMD, PIC_EOI);
			return true;
		}
	}
	return false;
}
