// SPDX-License-Identifier: GPL-2.0-only
#include "hosttest.h"
#include <hix/kernel.h>
#include <aether/pmm.h>
#include <aether/kalloc.h>
#include <aether/task.h>

static struct ae_msg make_msg(ae_port_name_t dest, ae_port_name_t reply,
				const char *text, u32 id)
{
	struct ae_msg m;

	memset(&m, 0, sizeof(m));
	strlcpy((char *)m.data, text, sizeof(m.data));
	m.header.size = sizeof(m.header) + strlen(text) + 1;
	m.header.remote_port = dest;
	m.header.local_port = reply;
	m.header.id = id;
	return m;
}

int main(void)
{
	static struct task a, b;
	ae_port_name_t pa, pb, to_a, to_b, bogus;
	struct ae_msg m, r;
	int i;

	kalloc_init_region(malloc(1 << 20), 1 << 20);
	ipc_space_init(&a.space);
	ipc_space_init(&b.space);

	/* A owns a port; nothing to receive yet */
	CHECK(ae_port_allocate(&a, &pa) == AE_OK && pa != AE_PORT_NULL);
	CHECK(ipc_space_count(&a.space) == 1);
	CHECK(ae_msg_receive(&a, pa, &r, true, 0) == AE_RCV_TIMED_OUT);

	/* B cannot send without a right; names are not guessable */
	m = make_msg(0x101, 0, "x", 1);
	CHECK(ae_msg_send(&b, &m, true, 0) == AE_SEND_INVALID_DEST);
	CHECK(ae_port_insert_send(&b, pa) == AE_INVALID_NAME);

	/* publish through bootstrap, B looks it up */
	CHECK(bootstrap_register(&a, "test.svc", pa) == AE_OK);
	CHECK(bootstrap_register(&a, "test.svc", pa) == AE_NAME_EXISTS);
	CHECK(bootstrap_look_up(&b, "nope", &to_a) == AE_NOT_FOUND);
	CHECK(bootstrap_look_up(&b, "test.svc", &to_a) == AE_OK);

	/* request/reply: B sends with its own port as the reply port */
	CHECK(ae_port_allocate(&b, &pb) == AE_OK);
	m = make_msg(to_a, pb, "ping", 7);
	CHECK(ae_msg_send(&b, &m, true, 0) == AE_OK);

	CHECK(ae_msg_receive(&a, pa, &r, true, 0) == AE_OK);
	CHECK_STR((char *)r.data, "ping");
	CHECK(r.header.id == 7 && r.header.local_port == pa);
	CHECK(r.header.remote_port != AE_PORT_NULL);	/* A got a send right */
	to_b = r.header.remote_port;

	m = make_msg(to_b, 0, "pong", 8);
	CHECK(ae_msg_send(&a, &m, true, 0) == AE_OK);
	CHECK(ae_msg_receive(&b, pb, &r, true, 0) == AE_OK);
	CHECK_STR((char *)r.data, "pong");

	/* the same message again must not allocate a second name */
	m = make_msg(to_a, pb, "again", 9);
	ae_msg_send(&b, &m, true, 0);
	ae_msg_receive(&a, pa, &r, true, 0);
	CHECK(r.header.remote_port == to_b);

	/* queue limit: the 17th message times out instead of blocking */
	for (i = 0; i < IPC_PORT_QLIMIT; i++) {
		m = make_msg(to_a, 0, "fill", i);
		CHECK(ae_msg_send(&b, &m, true, 0) == AE_OK);
	}
	m = make_msg(to_a, 0, "overflow", 99);
	CHECK(ae_msg_send(&b, &m, true, 0) == AE_SEND_TIMED_OUT);
	for (i = 0; i < IPC_PORT_QLIMIT; i++) {	/* FIFO order */
		CHECK(ae_msg_receive(&a, pa, &r, true, 0) == AE_OK);
		CHECK(r.header.id == (u32)i);
	}

	/* bad sizes */
	m = make_msg(to_a, 0, "x", 1);
	m.header.size = 3;
	CHECK(ae_msg_send(&b, &m, true, 0) == AE_SEND_MSG_TOO_SMALL);
	m.header.size = sizeof(m) + 1;
	CHECK(ae_msg_send(&b, &m, true, 0) == AE_SEND_INVALID_HEADER);

	/* bad reply port name */
	m = make_msg(to_a, 0x7777, "x", 1);
	CHECK(ae_msg_send(&b, &m, true, 0) == AE_SEND_INVALID_HEADER);

	/* killing the receive right kills the port for everyone */
	m = make_msg(to_a, 0, "last", 1);
	ae_msg_send(&b, &m, true, 0);
	CHECK(ae_port_deallocate(&a, pa) == AE_OK);
	CHECK(ae_msg_send(&b, &m, true, 0) == AE_SEND_INVALID_DEST);
	CHECK(ae_msg_receive(&a, pa, &r, true, 0) == AE_RCV_INVALID_NAME);
	CHECK(bootstrap_look_up(&b, "test.svc", &bogus) == AE_NOT_FOUND);
	CHECK(ae_port_deallocate(&a, pa) == AE_INVALID_NAME);	/* stale */

	/* a dead name is free to be registered again by a new service */
	CHECK(ae_port_allocate(&a, &pa) == AE_OK);
	CHECK(bootstrap_register(&a, "test.svc", pa) == AE_OK);

	/* destroying a whole space releases everything */
	ipc_space_destroy(&a.space);
	CHECK(ipc_space_count(&a.space) == 0);
	ipc_space_destroy(&b.space);
	CHECK(ipc_space_count(&b.space) == 0);

	/* namespace exhaustion */
	for (i = 0; i < IPC_SPACE_SIZE; i++)
		CHECK(ae_port_allocate(&a, &pa) == AE_OK);
	CHECK(ae_port_allocate(&a, &pa) == AE_NO_SPACE);
	ipc_space_destroy(&a.space);

	TEST_DONE();
}
