/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * aether: port IPC.
 *
 * A port is a kernel-protected message queue. Tasks never see ports, only
 * names in their own port namespace (struct ipc_space), each backed by a
 * right: RECEIVE (exactly one task owns it) and/or SEND (many may hold it).
 * Messages carry a small inline payload and may name a reply port, which
 * the receiver gets as a send right - the classic request/reply pattern.
 *
 * Numbers (ae_ret_t, option bits, rights) are small integers, stable across releases.
 */
#ifndef _AETHER_IPC_H
#define _AETHER_IPC_H

#include <hix/types.h>

struct task;
struct ipc_port;

typedef int ae_ret_t;
typedef u32 ae_port_name_t;

#define AE_OK			0
#define AE_INVALID_ADDRESS		1
#define AE_NO_SPACE			3
#define AE_INVALID_ARGUMENT		4
#define AE_FAILURE			5
#define AE_RESOURCE_SHORTAGE		6
#define AE_NAME_EXISTS		13
#define AE_INVALID_NAME		15
#define AE_INVALID_RIGHT		17
#define AE_INVALID_VALUE		18
#define AE_NOT_FOUND			56

#define AE_SEND_INVALID_DEST		0x10000003
#define AE_SEND_TIMED_OUT		0x10000004
#define AE_SEND_MSG_TOO_SMALL		0x1000000c
#define AE_SEND_INVALID_HEADER	0x10000010
#define AE_RCV_INVALID_NAME		0x10004002
#define AE_RCV_TIMED_OUT		0x10004003
#define AE_RCV_PORT_DIED		0x10004009

#define AE_PORT_NULL			0

#define AE_PORT_RIGHT_SEND		0
#define AE_PORT_RIGHT_RECEIVE		1

#define AE_SEND_MSG			0x00000001
#define AE_RCV_MSG			0x00000002
#define AE_SEND_TIMEOUT		0x00000010
#define AE_RCV_TIMEOUT		0x00000100

#define AE_MSG_MAX_DATA		128
#define IPC_SPACE_SIZE			64
#define IPC_PORT_QLIMIT			16

struct ae_msg_header {
	u32 size;			/* header + payload, in bytes */
	ae_port_name_t remote_port;	/* send: destination; receive: reply */
	ae_port_name_t local_port;	/* send: reply port; receive: ours */
	u32 id;				/* free for the application */
};

struct ae_msg {
	struct ae_msg_header header;
	u8 data[AE_MSG_MAX_DATA];
};

/* a task's port namespace */
struct ipc_entry {
	struct ipc_port *port;
	u8 rights;			/* BIT(AE_PORT_RIGHT_*) */
	u8 gen;				/* bumped on reuse so stale names fail */
};

struct ipc_space {
	struct ipc_entry entries[IPC_SPACE_SIZE];
};

void ipc_init(void);
void ipc_space_init(struct ipc_space *space);
void ipc_space_destroy(struct ipc_space *space);
unsigned int ipc_space_count(const struct ipc_space *space);

/* new port, caller gets its receive right */
ae_ret_t ae_port_allocate(struct task *task, ae_port_name_t *name);
/* make a send right out of a receive right the task owns */
ae_ret_t ae_port_insert_send(struct task *task, ae_port_name_t name);
/* drop whatever rights @name carries; dropping RECEIVE kills the port */
ae_ret_t ae_port_deallocate(struct task *task, ae_port_name_t name);

/* timeouts are in milliseconds; 0 with the flag set means "don't wait" */
ae_ret_t ae_msg_send(struct task *task, const struct ae_msg *msg,
			    bool use_timeout, unsigned long timeout_ms);
ae_ret_t ae_msg_receive(struct task *task, ae_port_name_t name,
			       struct ae_msg *msg, bool use_timeout,
			       unsigned long timeout_ms);
/* the combined trap: send (optional) then receive on @rcv_name (optional) */
ae_ret_t ae_msg(struct task *task, struct ae_msg *msg, u32 option,
		       ae_port_name_t rcv_name, unsigned long timeout_ms);

/* the bootstrap name service: look up ports by string name */
ae_ret_t bootstrap_register(struct task *task, const char *service,
				 ae_port_name_t port);
ae_ret_t bootstrap_look_up(struct task *task, const char *service,
				ae_port_name_t *out);

#endif /* _AETHER_IPC_H */
