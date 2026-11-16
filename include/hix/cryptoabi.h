/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * hix crypto ABI: the small language-neutral surface between ring 3, qubix
 * and the Rust modules in the kernel. See docs/CRYPTO.md.
 *
 *   r = SYS_crypto(op, &req);
 *
 * Keys and tags are raw bytes; every pointer is checked by qubix before the
 * kernel copies the buffers. The request is deliberately fixed-width: it is
 * shared by the kernel, the SDK and host test harnesses.
 */
#ifndef _HIX_CRYPTOABI_H
#define _HIX_CRYPTOABI_H

#define HIX_CRYPTO_ABI		1

/* operations */
#define HIX_CRYPTO_SELFTEST	0	/* run RFC 8439 vectors */
#define HIX_CRYPTO_CHACHA20	1	/* flags = initial 32-bit counter */
#define HIX_CRYPTO_AEAD_SEAL	2	/* encrypt and write tag */
#define HIX_CRYPTO_AEAD_OPEN	3	/* verify tag, then decrypt */

/* algorithm parameters */
#define HIX_CRYPTO_KEY_LEN	32
#define HIX_CRYPTO_NONCE_LEN	12
#define HIX_CRYPTO_TAG_LEN	16
#define HIX_CRYPTO_AAD_MAX	4096
#define HIX_CRYPTO_DATA_MAX	(64 * 1024)

struct hix_crypto_req {
	unsigned int abi;	/* HIX_CRYPTO_ABI */
	unsigned int flags;	/* operation-specific */
	unsigned int key_len;
	unsigned int nonce_len;
	unsigned int aad_len;
	unsigned int in_len;
	unsigned int out_len;
	unsigned int tag_len;
	const void *key;
	const void *nonce;
	const void *aad;
	const void *in;
	void *out;
	void *tag;
	long result;		/* bytes produced, or 0 for SELFTEST */
};

#endif /* _HIX_CRYPTOABI_H */
