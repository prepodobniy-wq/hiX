// SPDX-License-Identifier: GPL-2.0-only
/*
 * The few things compiled C++ expects from its runtime. Kept in C: the
 * kernel does not depend on a C++ compiler, it only provides this much for
 * the layers above it that happen to be written in C++.
 */
#include <hix/kernel.h>

typedef void (*ctor_fn)(void);

extern ctor_fn __init_array_start[];
extern ctor_fn __init_array_end[];

void *__dso_handle;

/* a call through a pure virtual function slot means a corrupted object */
void __cxa_pure_virtual(void)
{
	panic("C++: pure virtual function called");
}

/* static objects live forever: nothing runs destructors at shutdown */
int __cxa_atexit(void (*fn)(void *), void *arg, void *dso)
{
	(void)fn;
	(void)arg;
	(void)dso;
	return 0;
}

/* run the constructors of global C++ objects; called once from kernel_main */
void cxx_init(void)
{
	ctor_fn *f;

	for (f = __init_array_start; f < __init_array_end; f++)
		(*f)();
}
