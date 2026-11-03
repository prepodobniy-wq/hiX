/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * hix: basic types.
 *
 * Only freestanding compiler headers are used, so this file is safe both for
 * the kernel proper and for host-side unit tests.
 */
#ifndef _HIX_TYPES_H
#define _HIX_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

/* physical addresses are 32 bit on i386 (no PAE) */
typedef u32 phys_addr_t;

#ifdef HIX_HOST_TEST
#define pid_t		hix_pid_t
#define ssize_t		hix_ssize_t
#define off_t		hix_off_t
#endif

typedef s32 pid_t;
typedef s32 ssize_t;
typedef s32 off_t;

#define PAGE_SHIFT	12
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE - 1))

#ifdef HIX_HOST_TEST
/*
 * Host-side unit tests link our libc-ish helpers next to the real libc.
 * Rename ours so the two never clash. Include libc headers BEFORE hix ones.
 */
#define memset		hix_memset
#define memcpy		hix_memcpy
#define memmove		hix_memmove
#define memcmp		hix_memcmp
#define strlen		hix_strlen
#define strnlen		hix_strnlen
#define strcmp		hix_strcmp
#define strncmp		hix_strncmp
#define strncpy		hix_strncpy
#define strlcpy		hix_strlcpy
#define strchr		hix_strchr
#define strrchr		hix_strrchr
#define strstr		hix_strstr
#define snprintf	hix_snprintf
#define vsnprintf	hix_vsnprintf
#define printk		hix_printk
#endif

#endif /* _HIX_TYPES_H */
