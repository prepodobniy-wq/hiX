// SPDX-License-Identifier: GPL-2.0-only
/*
 * 16550 UART on COM1: a printk console and a second keyboard.
 * Run QEMU with `-serial stdio` (make run-serial) to use it.
 */
#define pr_fmt(fmt) "serial: " fmt

#include <hix/kernel.h>
#include <hix/driver.h>
#include <hix/console.h>
#include <asm/io.h>
#include <asm/trap.h>
#include <qubix/tty.h>

#define COM1		0x3F8
#define REG_DATA	0	/* also divisor low (DLAB) */
#define REG_IER		1	/* also divisor high (DLAB) */
#define REG_FCR		2
#define REG_LCR		3
#define REG_MCR		4
#define REG_LSR		5
#define LSR_RX_READY	0x01
#define LSR_TX_EMPTY	0x20
#define COM1_IRQ	4

static void serial_putc(char c)
{
	int spin = 100000;

	while (!(inb(COM1 + REG_LSR) & LSR_TX_EMPTY) && --spin)
		;
	outb(COM1 + REG_DATA, (u8)c);
}

static void serial_write(struct console *con, int level, const char *s,
			 size_t n)
{
	(void)con;
	(void)level;
	while (n--) {
		if (*s == '\n')
			serial_putc('\r');
		serial_putc(*s++);
	}
}

static struct console serial_console = {
	.name = "serial",
	.max_level = 7,
	.write = serial_write,
};

static void serial_irq(int irq, void *dev)
{
	(void)irq;
	(void)dev;
	while (inb(COM1 + REG_LSR) & LSR_RX_READY) {
		int c = inb(COM1 + REG_DATA);

		if (c == '\r')
			c = '\n';
		else if (c == 0x7F)
			c = '\b';
		tty_input(c);
	}
}

/* a UART answers its own loopback test; an empty port floats high */
static int serial_probe(void)
{
	outb(COM1 + REG_IER, 0);
	outb(COM1 + REG_LCR, 0x80);		/* DLAB on */
	outb(COM1 + REG_DATA, 1);		/* 115200 baud */
	outb(COM1 + REG_IER, 0);
	outb(COM1 + REG_LCR, 0x03);		/* 8N1 */
	outb(COM1 + REG_FCR, 0xC7);		/* FIFO on, cleared */
	outb(COM1 + REG_MCR, 0x1E);		/* loopback */
	outb(COM1 + REG_DATA, 0xAE);
	if (inb(COM1 + REG_DATA) != 0xAE)
		return -ENODEV;
	outb(COM1 + REG_MCR, 0x0B);		/* normal, OUT2 enables IRQ */
	return 0;
}

static int serial_start(void)
{
	register_console(&serial_console);
	if (request_irq(COM1_IRQ, serial_irq, "serial", NULL) == 0)
		outb(COM1 + REG_IER, 0x01);	/* interrupt on received data */
	return 0;
}

static struct hix_driver serial_driver = {
	.name = "ns16550",
	.class = "serial",
	.level = DRV_LEVEL_CONSOLE,
	.probe = serial_probe,
	.start = serial_start,
};
HIX_DRIVER(serial_driver);
