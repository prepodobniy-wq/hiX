// SPDX-License-Identifier: GPL-2.0-only
/* The C++ support: operator new on the kernel heap, virtuals, constructors. */
extern "C" {
#include "hosttest.h"
#include <hix/kernel.h>
#include <aether/kalloc.h>
}

static int ctor_ran;

struct Registrar {
	Registrar() { ctor_ran = 42; }
};
static Registrar registrar;		/* a global object, built before main */

struct Shape {
	virtual ~Shape() {}
	virtual int area() const = 0;
};

struct Rect : Shape {
	int w, h;
	Rect(int w_, int h_) : w(w_), h(h_) {}
	int area() const override { return w * h; }
};

struct Square : Rect {
	explicit Square(int s) : Rect(s, s) {}
};

int main()
{
	static char arena[1 << 16] __attribute__((aligned(16)));
	struct kalloc_stats st;
	Shape *s[3];
	int sum = 0;
	int *v;

	kalloc_init_region(arena, sizeof(arena));

	CHECK(ctor_ran == 42);

	s[0] = new Rect(3, 4);
	s[1] = new Square(5);
	s[2] = new Rect(1, 1);
	for (Shape *p : s)
		sum += p->area();
	CHECK(sum == 12 + 25 + 1);

	kalloc_get_stats(&st);
	CHECK(st.blocks_used == 3);
	for (Shape *p : s)
		delete p;
	kalloc_get_stats(&st);
	CHECK(st.blocks_used == 0);

	v = new int[100];
	CHECK(v && ((unsigned long)v & 15) == 0);
	for (int i = 0; i < 100; i++)
		v[i] = i;
	CHECK(v[99] == 99);
	delete[] v;
	CHECK(kalloc_check() == 0);

	TEST_DONE();
}
