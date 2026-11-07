/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * C linkage for headers that C++ code includes. The kernel itself is C; the
 * parts written in C++ talk to it, and to the C parts of hix, through plain
 * C functions.
 */
#ifndef _HIX_LINKAGE_H
#define _HIX_LINKAGE_H

#ifdef __cplusplus
#define HIX_BEGIN_C	extern "C" {
#define HIX_END_C	}
#else
#define HIX_BEGIN_C
#define HIX_END_C
#endif

#endif /* _HIX_LINKAGE_H */
