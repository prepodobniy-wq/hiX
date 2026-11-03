/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hix/hix.h - identity of the kernel and the cross-layer entry points
 *
 * hix  ("hi kernel", "hi is not unix") is the hybrid kernel as a whole.
 * It is built from three cooperating layers:
 *
 *   aether - the core: address spaces, tasks, threads,
 *            scheduler, ports and message passing
 *   qubix  - the process personality: syscalls, processes, VFS, shell
 *   steel  - the graphics stack: framebuffer, console, GPU-ish
 *            command buffers
 */
#ifndef _HIX_HIX_H
#define _HIX_HIX_H

#include <hix/types.h>

#define HIX_NAME	"hix"
#define HIX_VERSION	"0.2.0"
#define HIX_CODENAME	"hi kernel"
#define HIX_TAGLINE	"hi is not unix"
#define HIX_ARCH	"i386"

#define HZ		100

struct multiboot_info;

/* kernel/main */
void hix_main(u32 magic, const struct multiboot_info *mbi);

/* hix/panic.c */
void panic(const char *fmt, ...) __noreturn __printf(1, 2);

/* hix/pit.c */
extern volatile u32 jiffies;
u32 uptime_ms(void);

/* hix/reboot.c */
void hix_reboot(void) __noreturn;
void hix_halt(void) __noreturn;

#endif /* _HIX_HIX_H */
