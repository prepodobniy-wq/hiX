// SPDX-License-Identifier: GPL-2.0-only
/*
 * aether: port IPC, see include/aether/ipc.h for the model.
 *
 * Locking: one global lock (interrupts off) guards every port, queue, space
 * entry and the bootstrap table. Blocking waits sleep *inside* the critical
 * section; waitq_wait_irq() lets other threads run meanwhile, so after every
 * wakeup the code re-validates what it needs from scratch. A port that a
 * sleeper depends on is kept alive by a temporary reference.
 */
#define pr_fmt(fmt) "aether: ipc: " fmt

#include <hix/kernel.h>
#include <hix/list.h>
#include <hix/spinlock.h>
#include <aether/kalloc.h>
#include <aether/task.h>
#include <aether/waitq.h>

struct ipc_port {
	u32 refs;
	bool dead;			/* receive right destroyed */
	struct list_head queue;
	unsigned int qlen;
	struct waitq recv_wq;
	struct waitq send_wq;
};

struct ipc_msg {
	struct list_head node;
	struct ipc_port *reply;		/* reference held while queued */
	u32 id;
	u32 size;
	u8 data[AE_MSG_MAX_DATA];
};

#define BOOTSTRAP_MAX	16
#define SERVICE_NAME_MAX 24

static DEFINE_SPINLOCK(ipc_lock);

static struct {
	char name[SERVICE_NAME_MAX];
	struct ipc_port *port;		/* reference held, NULL = free slot */
} bootstrap_table[BOOTSTRAP_MAX];

#define RIGHT_SEND	BIT(AE_PORT_RIGHT_SEND)
#define RIGHT_RECV	BIT(AE_PORT_RIGHT_RECEIVE)

/* ---- ports ----------------------------------------------------------- */

static void port_ref(struct ipc_port *port)
{
	port->refs++;
}

static void port_unref(struct ipc_port *port)
{
	BUG_ON(port->refs == 0);
	if (--port->refs == 0) {
		BUG_ON(!port->dead);
		kfree(port);
	}
}

static void msg_discard(struct ipc_msg *msg)
{
	if (msg->reply)
		port_unref(msg->reply);
	kfree(msg);
}

/* the receive right is gone: flush the queue, wake everybody who waits */
static void port_kill(struct ipc_port *port)
{
	if (port->dead)
		return;
	port->dead = true;
	while (!list_empty(&port->queue)) {
		struct ipc_msg *msg = list_first_entry(&port->queue,
						       struct ipc_msg, node);

		list_del(&msg->node);
		msg_discard(msg);
	}
	port->qlen = 0;
	waitq_wake_all(&port->recv_wq);
	waitq_wake_all(&port->send_wq);
}

/* ---- namespaces ------------------------------------------------------ */

/*
 * A name is (generation << 8) | (slot + 1). The generation changes each time
 * a slot is reused, so a stale name never silently refers to a new port.
 */
static struct ipc_entry *entry_lookup(struct ipc_space *space,
				      ae_port_name_t name)
{
	u32 slot = name & 0xFF;
	struct ipc_entry *e;

	if (slot == 0 || slot > IPC_SPACE_SIZE)
		return NULL;
	e = &space->entries[slot - 1];
	if (!e->port || e->gen != ((name >> 8) & 0xFF))
		return NULL;
	return e;
}

static struct ipc_entry *entry_alloc(struct ipc_space *space,
				     ae_port_name_t *name)
{
	u32 i;

	for (i = 0; i < IPC_SPACE_SIZE; i++) {
		struct ipc_entry *e = &space->entries[i];

		if (e->port)
			continue;
		e->gen++;
		*name = ((u32)e->gen << 8) | (i + 1);
		return e;
	}
	return NULL;
}

static ae_port_name_t entry_name(struct ipc_space *space,
				   const struct ipc_entry *e)
{
	return ((u32)e->gen << 8) | (u32)(e - space->entries + 1);
}

/* give the space a send right to @port, reusing its name if it has one */
static ae_ret_t insert_send_right(struct ipc_space *space,
				       struct ipc_port *port,
				       ae_port_name_t *name)
{
	struct ipc_entry *e;
	u32 i;

	for (i = 0; i < IPC_SPACE_SIZE; i++) {
		e = &space->entries[i];
		if (e->port == port) {
			e->rights |= RIGHT_SEND;
			*name = entry_name(space, e);
			return AE_OK;
		}
	}
	e = entry_alloc(space, name);
	if (!e)
		return AE_NO_SPACE;
	port_ref(port);
	e->port = port;
	e->rights = RIGHT_SEND;
	return AE_OK;
}

void ipc_init(void)
{
	/* nothing to set up: all state is static */
}

void ipc_space_init(struct ipc_space *space)
{
	memset(space, 0, sizeof(*space));
}

void ipc_space_destroy(struct ipc_space *space)
{
	unsigned long flags;
	u32 i;

	spin_lock_irqsave(&ipc_lock, flags);
	for (i = 0; i < IPC_SPACE_SIZE; i++) {
		struct ipc_entry *e = &space->entries[i];
		struct ipc_port *port = e->port;

		if (!port)
			continue;
		if (e->rights & RIGHT_RECV)
			port_kill(port);
		e->port = NULL;
		e->rights = 0;
		port_unref(port);
	}
	spin_unlock_irqrestore(&ipc_lock, flags);
}

unsigned int ipc_space_count(const struct ipc_space *space)
{
	unsigned int n = 0;
	u32 i;

	for (i = 0; i < IPC_SPACE_SIZE; i++)
		if (space->entries[i].port)
			n++;
	return n;
}

ae_ret_t ae_port_allocate(struct task *task, ae_port_name_t *name)
{
	struct ipc_port *port = kzalloc(sizeof(*port));
	struct ipc_entry *e;
	unsigned long flags;

	if (!port)
		return AE_RESOURCE_SHORTAGE;
	INIT_LIST_HEAD(&port->queue);
	waitq_init(&port->recv_wq);
	waitq_init(&port->send_wq);

	spin_lock_irqsave(&ipc_lock, flags);
	e = entry_alloc(&task->space, name);
	if (!e) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		kfree(port);
		return AE_NO_SPACE;
	}
	e->port = port;
	e->rights = RIGHT_RECV;
	port->refs = 1;
	spin_unlock_irqrestore(&ipc_lock, flags);
	return AE_OK;
}

ae_ret_t ae_port_insert_send(struct task *task, ae_port_name_t name)
{
	struct ipc_entry *e;
	unsigned long flags;
	ae_ret_t kr = AE_OK;

	spin_lock_irqsave(&ipc_lock, flags);
	e = entry_lookup(&task->space, name);
	if (!e)
		kr = AE_INVALID_NAME;
	else if (!(e->rights & RIGHT_RECV))
		kr = AE_INVALID_RIGHT;
	else
		e->rights |= RIGHT_SEND;
	spin_unlock_irqrestore(&ipc_lock, flags);
	return kr;
}

ae_ret_t ae_port_deallocate(struct task *task, ae_port_name_t name)
{
	struct ipc_entry *e;
	struct ipc_port *port;
	unsigned long flags;

	spin_lock_irqsave(&ipc_lock, flags);
	e = entry_lookup(&task->space, name);
	if (!e) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		return AE_INVALID_NAME;
	}
	port = e->port;
	if (e->rights & RIGHT_RECV)
		port_kill(port);
	e->port = NULL;
	e->rights = 0;
	port_unref(port);
	spin_unlock_irqrestore(&ipc_lock, flags);
	return AE_OK;
}

/* ---- messages -------------------------------------------------------- */

static unsigned long to_ticks(unsigned long ms)
{
	unsigned long t = msecs_to_jiffies(ms);

	return t ? t : 1;
}

ae_ret_t ae_msg_send(struct task *task, const struct ae_msg *msg,
			    bool use_timeout, unsigned long timeout_ms)
{
	struct ipc_msg *node;
	struct ipc_entry *e;
	struct ipc_port *dest;
	unsigned long flags;
	ae_ret_t kr = AE_OK;
	bool timed_out = false;
	u32 size = msg->header.size;

	if (size < sizeof(struct ae_msg_header))
		return AE_SEND_MSG_TOO_SMALL;
	if (size > sizeof(struct ae_msg))
		return AE_SEND_INVALID_HEADER;

	node = kzalloc(sizeof(*node));
	if (!node)
		return AE_RESOURCE_SHORTAGE;
	node->size = size;
	node->id = msg->header.id;
	memcpy(node->data, msg->data, size - sizeof(struct ae_msg_header));

	spin_lock_irqsave(&ipc_lock, flags);

	e = entry_lookup(&task->space, msg->header.remote_port);
	if (!e || !(e->rights & RIGHT_SEND)) {
		kr = AE_SEND_INVALID_DEST;
		goto fail;
	}
	dest = e->port;

	if (msg->header.local_port != AE_PORT_NULL) {
		e = entry_lookup(&task->space, msg->header.local_port);
		if (!e) {
			kr = AE_SEND_INVALID_HEADER;
			goto fail;
		}
		node->reply = e->port;
		port_ref(node->reply);
	}

	port_ref(dest);			/* we may sleep below */
	while (!dest->dead && dest->qlen >= IPC_PORT_QLIMIT) {
		if (timed_out || (use_timeout && timeout_ms == 0)) {
			kr = AE_SEND_TIMED_OUT;
			break;
		}
		if (waitq_wait_irq(&dest->send_wq,
				   use_timeout ? to_ticks(timeout_ms) : 0))
			timed_out = true;	/* one last look at the queue */
	}
	if (kr == AE_OK && dest->dead)
		kr = AE_SEND_INVALID_DEST;
	if (kr == AE_OK) {
		list_add_tail(&node->node, &dest->queue);
		dest->qlen++;
		waitq_wake_one(&dest->recv_wq);
		node = NULL;
	}
	port_unref(dest);
fail:
	if (node)
		msg_discard(node);
	spin_unlock_irqrestore(&ipc_lock, flags);
	return kr;
}

ae_ret_t ae_msg_receive(struct task *task, ae_port_name_t name,
			       struct ae_msg *msg, bool use_timeout,
			       unsigned long timeout_ms)
{
	struct ipc_entry *e;
	struct ipc_port *port;
	struct ipc_msg *node = NULL;
	unsigned long flags;
	ae_ret_t kr = AE_OK;
	bool timed_out = false;

	spin_lock_irqsave(&ipc_lock, flags);
	e = entry_lookup(&task->space, name);
	if (!e || !(e->rights & RIGHT_RECV)) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		return AE_RCV_INVALID_NAME;
	}
	port = e->port;
	port_ref(port);			/* we may sleep below */

	for (;;) {
		if (!list_empty(&port->queue)) {
			node = list_first_entry(&port->queue, struct ipc_msg,
						node);
			list_del(&node->node);
			port->qlen--;
			waitq_wake_one(&port->send_wq);
			break;
		}
		if (port->dead) {
			kr = AE_RCV_PORT_DIED;
			break;
		}
		if (timed_out || (use_timeout && timeout_ms == 0)) {
			kr = AE_RCV_TIMED_OUT;
			break;
		}
		if (waitq_wait_irq(&port->recv_wq,
				   use_timeout ? to_ticks(timeout_ms) : 0))
			timed_out = true;	/* one last look at the queue */
	}

	if (node) {
		ae_port_name_t reply_name = AE_PORT_NULL;

		if (node->reply) {
			if (insert_send_right(&task->space, node->reply,
					      &reply_name) != AE_OK)
				reply_name = AE_PORT_NULL;
			port_unref(node->reply);
			node->reply = NULL;
		}
		msg->header.size = node->size;
		msg->header.remote_port = reply_name;
		msg->header.local_port = name;
		msg->header.id = node->id;
		memcpy(msg->data, node->data,
		       node->size - sizeof(struct ae_msg_header));
		kfree(node);
	}
	port_unref(port);
	spin_unlock_irqrestore(&ipc_lock, flags);
	return kr;
}

ae_ret_t ae_msg(struct task *task, struct ae_msg *msg, u32 option,
		       ae_port_name_t rcv_name, unsigned long timeout_ms)
{
	ae_ret_t kr;

	if (option & AE_SEND_MSG) {
		kr = ae_msg_send(task, msg, option & AE_SEND_TIMEOUT,
				   timeout_ms);
		if (kr != AE_OK)
			return kr;
	}
	if (option & AE_RCV_MSG)
		return ae_msg_receive(task, rcv_name, msg,
					option & AE_RCV_TIMEOUT, timeout_ms);
	return AE_OK;
}

/* ---- bootstrap name service ----------------------------------------- */

ae_ret_t bootstrap_register(struct task *task, const char *service,
				 ae_port_name_t name)
{
	struct ipc_entry *e;
	unsigned long flags;
	ae_ret_t kr = AE_OK;
	int i, slot = -1;

	if (!service[0] || strnlen(service, SERVICE_NAME_MAX) >= SERVICE_NAME_MAX)
		return AE_INVALID_ARGUMENT;

	spin_lock_irqsave(&ipc_lock, flags);
	e = entry_lookup(&task->space, name);
	if (!e) {
		kr = AE_INVALID_NAME;
		goto out;
	}
	if (!(e->rights & RIGHT_RECV)) {
		kr = AE_INVALID_RIGHT;
		goto out;
	}

	for (i = 0; i < BOOTSTRAP_MAX; i++) {
		if (!bootstrap_table[i].port) {
			if (slot < 0)
				slot = i;
		} else if (!strcmp(bootstrap_table[i].name, service)) {
			if (!bootstrap_table[i].port->dead) {
				kr = AE_NAME_EXISTS;
				goto out;
			}
			/* the old service died: take over its name */
			port_unref(bootstrap_table[i].port);
			bootstrap_table[i].port = NULL;
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		kr = AE_NO_SPACE;
		goto out;
	}
	strlcpy(bootstrap_table[slot].name, service, SERVICE_NAME_MAX);
	bootstrap_table[slot].port = e->port;
	port_ref(e->port);
	e->rights |= RIGHT_SEND;	/* the owner may also talk to itself */
out:
	spin_unlock_irqrestore(&ipc_lock, flags);
	return kr;
}

ae_ret_t bootstrap_look_up(struct task *task, const char *service,
				ae_port_name_t *out)
{
	unsigned long flags;
	ae_ret_t kr = AE_NOT_FOUND;
	int i;

	spin_lock_irqsave(&ipc_lock, flags);
	for (i = 0; i < BOOTSTRAP_MAX; i++) {
		struct ipc_port *port = bootstrap_table[i].port;

		if (!port || port->dead ||
		    strcmp(bootstrap_table[i].name, service))
			continue;
		kr = insert_send_right(&task->space, port, out);
		break;
	}
	spin_unlock_irqrestore(&ipc_lock, flags);
	return kr;
}
