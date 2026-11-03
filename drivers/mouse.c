// SPDX-License-Identifier: GPL-2.0-only
/*
 * PS/2 mouse, standard three-byte protocol, on the i8042 auxiliary port.
 *
 * start() talks to the controller with bounded waits, so a machine without
 * a PS/2 mouse (or a controller that does not answer) just reports the
 * driver as failed instead of hanging the boot.
 */
#define pr_fmt(fmt) "mouse: " fmt

#include <hix/kernel.h>
#include <hix/driver.h>
#include <asm/io.h>
#include <asm/trap.h>
#include <qubix/input.h>

#define KBD_DATA	0x60
#define KBD_CMD		0x64
#define KBD_STATUS	0x64

#define ST_OUT_FULL	0x01
#define ST_IN_FULL	0x02
#define ST_AUX_DATA	0x20

#define CMD_READ_CFG	0x20
#define CMD_WRITE_CFG	0x60
#define CMD_AUX_ENABLE	0xA8
#define CMD_AUX_WRITE	0xD4

#define CFG_AUX_IRQ	0x02
#define CFG_AUX_CLOCK_OFF	0x20

#define MOUSE_IRQ	12
#define SPIN_MAX	100000

static u8 packet[3];
static int packet_len;

static bool wait_write(void)
{
	int i;

	for (i = 0; i < SPIN_MAX; i++)
		if (!(inb(KBD_STATUS) & ST_IN_FULL))
			return true;
	return false;
}

static bool wait_read(void)
{
	int i;

	for (i = 0; i < SPIN_MAX; i++)
		if (inb(KBD_STATUS) & ST_OUT_FULL)
			return true;
	return false;
}

static bool ctl_cmd(u8 cmd)
{
	if (!wait_write())
		return false;
	outb(KBD_CMD, cmd);
	return true;
}

static bool ctl_data(u8 data)
{
	if (!wait_write())
		return false;
	outb(KBD_DATA, data);
	return true;
}

/* send a byte to the mouse itself and wait for its acknowledge (0xFA) */
static bool mouse_cmd(u8 data)
{
	if (!ctl_cmd(CMD_AUX_WRITE) || !ctl_data(data) || !wait_read())
		return false;
	return inb(KBD_DATA) == 0xFA;
}

static void mouse_irq(int irq, void *dev)
{
	u8 byte = inb(KBD_DATA);

	(void)irq;
	(void)dev;

	/* byte 0 always has bit 3 set: use that to find the packet start */
	if (packet_len == 0 && !(byte & 0x08))
		return;
	packet[packet_len++] = byte;
	if (packet_len < 3)
		return;
	packet_len = 0;

	if (packet[0] & 0xC0)			/* overflow: skip this packet */
		return;
	{
		int dx = (int)packet[1] - ((packet[0] & 0x10) ? 256 : 0);
		int dy = (int)packet[2] - ((packet[0] & 0x20) ? 256 : 0);

		input_mouse(dx, -dy, packet[0] & 0x07);
	}
}

static int mouse_start(void)
{
	u8 cfg;
	int guard = 64;

	while ((inb(KBD_STATUS) & ST_OUT_FULL) && guard--)
		inb(KBD_DATA);

	if (!ctl_cmd(CMD_AUX_ENABLE))
		return -ENODEV;
	if (!ctl_cmd(CMD_READ_CFG) || !wait_read())
		return -ENODEV;
	cfg = inb(KBD_DATA);
	cfg |= CFG_AUX_IRQ;
	cfg &= (u8)~CFG_AUX_CLOCK_OFF;
	if (!ctl_cmd(CMD_WRITE_CFG) || !ctl_data(cfg))
		return -ENODEV;

	if (!mouse_cmd(0xF6) || !mouse_cmd(0xF4)) {	/* defaults, then stream */
		pr_info("no PS/2 mouse\n");
		return -ENODEV;
	}
	packet_len = 0;
	return request_irq(MOUSE_IRQ, mouse_irq, "mouse", NULL);
}

static struct hix_driver mouse_driver = {
	.name = "ps2mouse",
	.class = "input",
	.level = DRV_LEVEL_INPUT,
	.start = mouse_start,
};
HIX_DRIVER(mouse_driver);
