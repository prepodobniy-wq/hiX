// SPDX-License-Identifier: GPL-2.0-only
/*
 * Gantry registry: nodes, properties, the driver list and matching.
 * Nothing here talks to hardware, so the host tests run it as it is.
 */
extern "C" {
#include <hix/kernel.h>
}
#include <gantry/gantry.h>

namespace gantry {

static Node *the_root;
static DriverFactory *factories;

static void copy(char *dst, const char *src, size_t cap)
{
	strlcpy(dst, src ? src : "", cap);
}

Node::Node(const char *cls, const char *name)
	: nprops_(0), parent_(nullptr), child_(nullptr), next_(nullptr)
{
	copy(cls_, cls, sizeof(cls_));
	copy(name_, name, sizeof(name_));
}

Node::~Node()
{
	while (child_) {
		Node *n = child_;

		child_ = n->next_;
		n->release();
	}
}

void Node::attach(Node *node)
{
	Node **link = &child_;

	node->parent_ = this;
	node->next_ = nullptr;
	while (*link)
		link = &(*link)->next_;
	*link = node;
}

void Node::set(const char *key, const char *val)
{
	int i;

	for (i = 0; i < nprops_; i++) {
		if (!strcmp(props_[i].key, key)) {
			copy(props_[i].val, val, VAL_LEN);
			return;
		}
	}
	if (nprops_ == MAX_PROPS)
		return;
	copy(props_[nprops_].key, key, KEY_LEN);
	copy(props_[nprops_].val, val, VAL_LEN);
	nprops_++;
}

void Node::set_hex(const char *key, u32 val)
{
	char buf[12];

	snprintf(buf, sizeof(buf), "%x", val);
	set(key, buf);
}

const char *Node::get(const char *key) const
{
	int i;

	for (i = 0; i < nprops_; i++)
		if (!strcmp(props_[i].key, key))
			return props_[i].val;
	return nullptr;
}

u32 Node::get_hex(const char *key, u32 dflt) const
{
	const char *s = get(key);
	u32 v = 0;

	if (!s || !*s)
		return dflt;
	for (; *s; s++) {
		char c = *s;

		if (c >= '0' && c <= '9')
			v = v * 16 + (u32)(c - '0');
		else if (c >= 'a' && c <= 'f')
			v = v * 16 + (u32)(c - 'a' + 10);
		else
			return dflt;
	}
	return v;
}

unsigned Node::count() const
{
	unsigned n = 1;

	for (const Node *c = child_; c; c = c->next_)
		n += c->count();
	return n;
}

Node *Node::find(const char *name)
{
	if (!strcmp(name_, name))
		return this;
	for (Node *c = child_; c; c = c->next_)
		if (Node *r = c->find(name))
			return r;
	return nullptr;
}

Node *Node::find_class(const char *cls)
{
	if (!strcmp(cls_, cls))
		return this;
	for (Node *c = child_; c; c = c->next_)
		if (Node *r = c->find_class(cls))
			return r;
	return nullptr;
}

Node *Node::driver() const
{
	for (Node *c = child_; c; c = c->next_)
		if (!strcmp(c->cls_, "Driver"))
			return c;
	return nullptr;
}

DriverFactory::DriverFactory(const char *n, const char *p, Driver *(*m)())
	: name(n), provider_class(p), make(m), next(nullptr)
{
	DriverFactory **link = &factories;

	while (*link)
		link = &(*link)->next;
	*link = this;
}

Node *Registry::root()
{
	if (!the_root)
		the_root = new Node("Platform", "Root");
	return the_root;
}

void Registry::reset()
{
	if (the_root)
		the_root->release();
	the_root = nullptr;
}

int Registry::factory_count()
{
	int n = 0;

	for (DriverFactory *f = factories; f; f = f->next)
		n++;
	return n;
}

int Registry::match(Node *node)
{
	int started = 0;

	if (strcmp(node->cls(), "Driver") && !node->driver()) {
		Driver *best = nullptr;
		int best_score = -1;

		for (DriverFactory *f = factories; f; f = f->next) {
			Driver *d;
			int score;

			if (strcmp(f->provider_class, node->cls()))
				continue;
			d = f->make();
			if (!d)
				continue;
			score = d->probe(node);
			if (score > best_score) {
				if (best)
					best->release();
				best = d;
				best_score = score;
			} else {
				d->release();
			}
		}
		if (best) {
			if (best->start(node) == 0) {
				best->set_hex("score", (u32)best_score);
				node->attach(best);
				started++;
			} else {
				best->release();
			}
		}
	}
	/* drivers attached just now are skipped by the class check above */
	for (Node *c = node->child(); c; c = c->next())
		if (strcmp(c->cls(), "Driver"))
			started += match(c);
	return started;
}

static int dump(const Node *n, int depth, char *buf, size_t size, int len)
{
	int i;

	if ((size_t)len >= size)
		return len;
	len += snprintf(buf + len, size - (size_t)len, "%*s+-o %s  <%s",
			depth * 2, "", n->name(), n->cls());
	for (i = 0; i < n->prop_count() && (size_t)len < size; i++)
		len += snprintf(buf + len, size - (size_t)len, " %s=%s",
				n->prop_key(i), n->prop_val(i));
	if ((size_t)len < size)
		len += snprintf(buf + len, size - (size_t)len, ">\n");
	for (const Node *c = n->child(); c; c = c->next())
		len = dump(c, depth + 1, buf, size, len);
	return len;
}

int Registry::format(const Node *node, char *buf, size_t size)
{
	int len = dump(node, 0, buf, size, 0);

	return (size_t)len < size ? len : (int)size - 1;
}

void add_pci_device(Node *bus, int dev, int fn, u32 id, u32 class_rev, u32 bar0)
{
	char name[Node::NAME_LEN];
	Node *n;

	snprintf(name, sizeof(name), "%02x.%d", dev, fn);
	n = new Node("PCIDevice", name);
	if (!n)
		return;
	n->set_hex("vendor", id & 0xFFFF);
	n->set_hex("device", id >> 16);
	n->set_hex("class", class_rev >> 16);
	n->set_hex("bar0", bar0);
	bus->attach(n);
}

}	/* namespace gantry */
