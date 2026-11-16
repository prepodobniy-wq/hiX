// SPDX-License-Identifier: GPL-2.0-only
/*
 * cryptotest: exercise the Rust crypto module from ring 3.
 *
 * This is the template for testing a cipher from another team: keep the
 * request ABI, replace the implementation in rust/hix-core, and run this
 * program (or generate a vector file with scripts/crypto_check.py).
 */
#include "../sdk/hix.h"

static int same(const unsigned char *a, const unsigned char *b, hx_size n)
{
	while (n--)
		if (*a++ != *b++)
			return 0;
	return 1;
}

static void fill(unsigned char *p, hx_size n, unsigned char seed)
{
	hx_size i;

	for (i = 0; i < n; i++)
		p[i] = (unsigned char)(seed + i * 17u);
}

static int selftest(void)
{
	struct hix_crypto_req req;
	long r;

	memset(&req, 0, sizeof(req));
	req.abi = HIX_CRYPTO_ABI;
	r = hx_crypto(HIX_CRYPTO_SELFTEST, &req);
	if (r < 0) {
		hx_printf("cryptotest: self-test: %ld\n", r);
		return 1;
	}
	hx_puts("cryptotest: RFC 8439 self-test ok\n");
	return 0;
}

static int roundtrip(void)
{
	static const unsigned char key[HIX_CRYPTO_KEY_LEN] = {
		0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
		0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
		0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
		0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
	};
	static const unsigned char nonce[HIX_CRYPTO_NONCE_LEN] = {
		0x07, 0x00, 0x00, 0x00, 0x40, 0x41, 0x42, 0x43,
		0x44, 0x45, 0x46, 0x47,
	};
	static const unsigned char aad[] = { 'h', 'i', 'x' };
	unsigned char plain[32], cipher[32], back[32], tag[HIX_CRYPTO_TAG_LEN];
	struct hix_crypto_req req;
	long r;

	fill(plain, sizeof(plain), 0x42);
	memset(&req, 0, sizeof(req));
	req.abi = HIX_CRYPTO_ABI;
	req.key = key;
	req.key_len = sizeof(key);
	req.nonce = nonce;
	req.nonce_len = sizeof(nonce);
	req.aad = aad;
	req.aad_len = sizeof(aad);
	req.in = plain;
	req.in_len = sizeof(plain);
	req.out = cipher;
	req.out_len = sizeof(cipher);
	req.tag = tag;
	req.tag_len = sizeof(tag);
	r = hx_crypto(HIX_CRYPTO_AEAD_SEAL, &req);
	if (r < 0) {
		hx_printf("cryptotest: seal: %ld\n", r);
		return 1;
	}
	if (same(plain, cipher, sizeof(plain))) {
		hx_puts("cryptotest: seal did not change the plaintext\n");
		return 1;
	}

	req.in = cipher;
	req.out = back;
	r = hx_crypto(HIX_CRYPTO_AEAD_OPEN, &req);
	if (r < 0) {
		hx_printf("cryptotest: open: %ld\n", r);
		return 1;
	}
	if (!same(plain, back, sizeof(plain))) {
		hx_puts("cryptotest: plaintext mismatch\n");
		return 1;
	}
	hx_puts("cryptotest: seal/open roundtrip ok\n");

	tag[0] ^= 0x80;
	r = hx_crypto(HIX_CRYPTO_AEAD_OPEN, &req);
	if (r != -74) {		/* -EBADMSG */
		hx_printf("cryptotest: bad tag accepted (%ld)\n", r);
		return 1;
	}
	hx_puts("cryptotest: tampered tag rejected\n");
	return 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (selftest())
		return 1;
	if (roundtrip())
		return 1;
	hx_puts("cryptotest: all checks passed\n");
	return 0;
}
