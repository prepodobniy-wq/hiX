/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _QUBIX_CRYPTO_H
#define _QUBIX_CRYPTO_H

#include <hix/types.h>
#include <hix/cryptoabi.h>

/* 1 when the Rust static library was linked into this kernel */
int hix_crypto_available(void);
/* 0 or -errno; safe to call from the shell and from boot */
int hix_crypto_selftest(void);
/* one line for sysctl hix.crypto */
int hix_crypto_format(char *buf, size_t size);
/* the SYS_crypto implementation, called from qubix/syscall.c */
long qubix_sys_crypto(long op, struct hix_crypto_req *req);

#endif /* _QUBIX_CRYPTO_H */
