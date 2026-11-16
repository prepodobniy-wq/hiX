/* SPDX-License-Identifier: GPL-2.0-only */
/* hixfetch: a logo and a few facts about the system, as plain text. */
#ifndef _QUBIX_HIXFETCH_H
#define _QUBIX_HIXFETCH_H

#include <hix/types.h>

/*
 * Write the report into @buf (lines end in '\n'); returns its length.
 * The facts come from sysctl, so any qubix program can call this.
 */
int hixfetch_text(char *buf, size_t size);

#endif /* _QUBIX_HIXFETCH_H */
