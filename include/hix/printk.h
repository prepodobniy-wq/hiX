/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _HIX_PRINTK_H
#define _HIX_PRINTK_H

#include <hix/types.h>
#include <hix/compiler.h>

#define KERN_SOH	"\001"
#define KERN_EMERG	KERN_SOH "0"
#define KERN_ALERT	KERN_SOH "1"
#define KERN_CRIT	KERN_SOH "2"
#define KERN_ERR	KERN_SOH "3"
#define KERN_WARNING	KERN_SOH "4"
#define KERN_NOTICE	KERN_SOH "5"
#define KERN_INFO	KERN_SOH "6"
#define KERN_DEBUG	KERN_SOH "7"

#define LOGLEVEL_DEFAULT	6

#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif

int printk(const char *fmt, ...) __printf(1, 2);

#define pr_emerg(fmt, ...)  printk(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_crit(fmt, ...)   printk(KERN_CRIT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err(fmt, ...)    printk(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn(fmt, ...)   printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_notice(fmt, ...) printk(KERN_NOTICE pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info(fmt, ...)   printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debug(fmt, ...)  printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)

/* the in-memory kernel log (what `dmesg` shows) */
size_t printk_log_read(char *buf, size_t len);

/* formatted printing without the log machinery */
int vsnprintf(char *buf, size_t size, const char *fmt, __builtin_va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...) __printf(3, 4);

#endif /* _HIX_PRINTK_H */
