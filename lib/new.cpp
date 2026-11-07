// SPDX-License-Identifier: GPL-2.0-only
/*
 * operator new and delete on top of the kernel heap. There are no exceptions
 * here, so a failed allocation returns nullptr, like new(std::nothrow).
 */
extern "C" {
#include <hix/kernel.h>
#include <aether/kalloc.h>
}

void *operator new(size_t size)
{
	return kmalloc(size);
}

void *operator new[](size_t size)
{
	return kmalloc(size);
}

void operator delete(void *p) noexcept
{
	kfree(p);
}

void operator delete[](void *p) noexcept
{
	kfree(p);
}

void operator delete(void *p, size_t) noexcept
{
	kfree(p);
}

void operator delete[](void *p, size_t) noexcept
{
	kfree(p);
}
