/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Gantry: the platform layer of hix.
 *
 * hix (the core), aether, qubix and steel are the kernels; Gantry is what
 * stands over all of them and is written in C++. It owns the registry, a tree
 * of nodes that describes the machine (platform, buses, devices, the drivers
 * running on them), and the driver model that fills the tree: a Driver
 * declares which kind of provider node it serves, scores a candidate in
 * probe(), and starts on the best match.
 *
 * The C kernel does not know about any of this. It calls gantry_boot() once,
 * after the C drivers are up, and reads the result through the C functions at
 * the end of this file. The Kit (SDK for user programs) will be built on the
 * same classes.
 *
 * Free-standing C++: no exceptions, no RTTI, no standard library.
 */
#ifndef _GANTRY_GANTRY_H
#define _GANTRY_GANTRY_H

#include <hix/types.h>
#include <hix/linkage.h>

#ifdef __cplusplus

namespace gantry {

/* reference counted base: nodes and drivers are heap objects */
class Object {
public:
	Object() : refs_(1) {}
	virtual ~Object() {}
	void retain() { refs_++; }
	void release()
	{
		if (--refs_ == 0)
			delete this;
	}
	int refs() const { return refs_; }

private:
	int refs_;
};

/* a node of the registry: a name, a class, a few properties, children */
class Node : public Object {
public:
	static constexpr int MAX_PROPS = 8;
	static constexpr int KEY_LEN = 16;
	static constexpr int VAL_LEN = 40;
	static constexpr int NAME_LEN = 24;

	Node(const char *cls, const char *name);
	~Node() override;

	const char *name() const { return name_; }
	const char *cls() const { return cls_; }
	Node *parent() const { return parent_; }
	Node *child() const { return child_; }
	Node *next() const { return next_; }

	/* the node becomes the last child of this one, which owns it */
	void attach(Node *node);

	void set(const char *key, const char *val);
	void set_hex(const char *key, u32 val);
	const char *get(const char *key) const;		/* nullptr when absent */
	u32 get_hex(const char *key, u32 dflt = 0) const;
	int prop_count() const { return nprops_; }
	const char *prop_key(int i) const { return props_[i].key; }
	const char *prop_val(int i) const { return props_[i].val; }

	/* this node and everything below it */
	unsigned count() const;
	/* first node below (or at) this one with that name / class */
	Node *find(const char *name);
	Node *find_class(const char *cls);
	/* the child that is a driver, nullptr when none is attached */
	Node *driver() const;

private:
	struct Prop {
		char key[KEY_LEN];
		char val[VAL_LEN];
	};

	char name_[NAME_LEN];
	char cls_[NAME_LEN];
	Prop props_[MAX_PROPS];
	int nprops_;
	Node *parent_, *child_, *next_;
};

/*
 * A driver is a node too, so it shows up in the registry under the node it
 * serves. probe() returns a score (the best score wins) or < 0 for "not mine";
 * start() brings the thing up and returns 0 on success.
 */
class Driver : public Node {
public:
	explicit Driver(const char *name) : Node("Driver", name) {}

	virtual int probe(Node *provider) = 0;
	virtual int start(Node *provider) = 0;
	virtual void stop() {}
};

/* a driver class announces itself with one of these, see GANTRY_DRIVER() */
struct DriverFactory {
	const char *name;
	const char *provider_class;	/* the node class it can serve */
	Driver *(*make)();
	DriverFactory *next;

	DriverFactory(const char *name, const char *provider_class,
		      Driver *(*make)());
};

#define GANTRY_DRIVER(type, provider)					\
	static gantry::Driver *make_##type() { return new type; }	\
	static gantry::DriverFactory factory_##type(#type, provider, make_##type)

class Registry {
public:
	static Node *root();
	/* match drivers to @node and below; returns how many were started */
	static int match(Node *node);
	/* an indented dump of the tree below @node */
	static int format(const Node *node, char *buf, size_t size);
	/* forget everything (tests) */
	static void reset();
	static int factory_count();
};

/* the registry as the platform builds it from the machine: gantry/platform.cpp */
void add_pci_device(Node *bus, int dev, int fn, u32 id, u32 class_rev, u32 bar0,
		    u32 bar1);

}	/* namespace gantry */

#endif /* __cplusplus */

HIX_BEGIN_C

/* build the registry from the machine and start the drivers that match */
void gantry_boot(void);
/* the tree as text; returns the length */
int gantry_format(char *buf, size_t size);
unsigned gantry_node_count(void);

HIX_END_C

#endif /* _GANTRY_GANTRY_H */
