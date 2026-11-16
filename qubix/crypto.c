// SPDX-License-Identifier: GPL-2.0-only
/*
 * qubix: the crypto bridge.
 *
 * The algorithms live in rust/hix-core (no_std, ring 0). This file is the
 * trusted border: it validates every user pointer, copies buffers into the
 * kernel, calls the Rust C ABI and wipes key material before returning.
 * Without a Rust toolchain the same syscall exists and returns -ENOSYS, so a
 * normal `make` still works on machines that cannot build the module.
 */
#define pr_fmt(fmt) "crypto: " fmt

#include <hix/kernel.h>
#include <aether/kalloc.h>
#include <qubix/crypto.h>
#include <qubix/syscall.h>

#ifdef HIX_HAVE_RUST
/*
 * The Rust panic handler and the personality routine the precompiled i686
 * core still references. panic=abort means the personality is never called;
 * the symbol only has to exist for the linker.
 */
void hix_rust_panic(const u8 *msg, size_t msg_len,
		    const u8 *file, size_t file_len, u32 line) __noreturn;

void hix_rust_panic(const u8 *msg, size_t msg_len,
		    const u8 *file, size_t file_len, u32 line)
{
	panic("rust: %.*s at %.*s:%u", (int)msg_len, (const char *)msg,
	      (int)file_len, (const char *)file, line);
}

void rust_eh_personality(void)
{
}

extern long hix_rust_selftest(void);
extern long hix_rust_abi_version(void);
extern long hix_rust_chacha20_xor(const u8 *key, const u8 *nonce, u32 counter,
				  u8 *data, u32 len);
extern long hix_rust_aead_seal(const u8 *key, const u8 *nonce,
			       const u8 *aad, u32 aad_len,
			       const u8 *input, u32 len, u8 *output, u8 *tag);
extern long hix_rust_aead_open(const u8 *key, const u8 *nonce,
			       const u8 *aad, u32 aad_len,
			       const u8 *input, u32 len, u8 *output,
			       const u8 *tag);

int hix_crypto_available(void)
{
	return 1;
}
#else
static long hix_rust_chacha20_xor(const u8 *key, const u8 *nonce, u32 counter,
				  u8 *data, u32 len)
{
	(void)key;
	(void)nonce;
	(void)counter;
	(void)data;
	(void)len;
	return -ENOSYS;
}

static long hix_rust_aead_seal(const u8 *key, const u8 *nonce,
			       const u8 *aad, u32 aad_len,
			       const u8 *input, u32 len, u8 *output, u8 *tag)
{
	(void)key;
	(void)nonce;
	(void)aad;
	(void)aad_len;
	(void)input;
	(void)len;
	(void)output;
	(void)tag;
	return -ENOSYS;
}

static long hix_rust_aead_open(const u8 *key, const u8 *nonce,
			       const u8 *aad, u32 aad_len,
			       const u8 *input, u32 len, u8 *output,
			       const u8 *tag)
{
	(void)key;
	(void)nonce;
	(void)aad;
	(void)aad_len;
	(void)input;
	(void)len;
	(void)output;
	(void)tag;
	return -ENOSYS;
}

int hix_crypto_available(void)
{
	return 0;
}
#endif

static int crypto_state = -1;

int hix_crypto_format(char *buf, size_t size)
{
	if (!hix_crypto_available())
		return snprintf(buf, size, "unavailable (kernel built without Rust)");
	if (crypto_state == 0)
		return snprintf(buf, size,
				"Rust, ChaCha20-Poly1305, selftest ok");
	if (crypto_state != -1)
		return snprintf(buf, size, "Rust, selftest failed (%d)",
				crypto_state);
	return snprintf(buf, size, "Rust, ChaCha20-Poly1305, not tested");
}

int hix_crypto_selftest(void)
{
#ifdef HIX_HAVE_RUST
	if (hix_rust_abi_version() != HIX_CRYPTO_ABI)
		return crypto_state = -EINVAL;
	crypto_state = (int)hix_rust_selftest();
#else
	crypto_state = -ENOSYS;
#endif
	return crypto_state;
}

static void wipe(void *ptr, size_t len)
{
	volatile u8 *p = ptr;

	while (len--)
		*p++ = 0;
}

/* copy a user buffer into the kernel; returns 0 or -errno */
static int copy_in(u8 **dst, const void *usrc, u32 len)
{
	*dst = NULL;
	if (!len)
		return 0;
	if (!qubix_user_range_ok(usrc, len, false))
		return -EFAULT;
	*dst = kmalloc(len);
	if (!*dst)
		return -ENOMEM;
	memcpy(*dst, usrc, len);
	return 0;
}

long qubix_sys_crypto(long op, struct hix_crypto_req *ureq)
{
	struct hix_crypto_req req;
	u8 key[HIX_CRYPTO_KEY_LEN];
	u8 nonce[HIX_CRYPTO_NONCE_LEN];
	u8 tag[HIX_CRYPTO_TAG_LEN];
	u8 *aad = NULL, *in = NULL, *out = NULL;
	long ret = 0;

	if (!qubix_user_range_ok(ureq, sizeof(req), true))
		return -EFAULT;
	memcpy(&req, ureq, sizeof(req));
	if (req.abi != HIX_CRYPTO_ABI)
		return -EINVAL;

	if (op == HIX_CRYPTO_SELFTEST) {
		ret = hix_crypto_selftest();
		req.result = 0;
		goto out_req;
	}
	if (!hix_crypto_available())
		return -ENOSYS;

	if (req.key_len != HIX_CRYPTO_KEY_LEN ||
	    !qubix_user_range_ok(req.key, HIX_CRYPTO_KEY_LEN, false) ||
	    req.nonce_len != HIX_CRYPTO_NONCE_LEN ||
	    !qubix_user_range_ok(req.nonce, HIX_CRYPTO_NONCE_LEN, false))
		return -EINVAL;
	if (req.aad_len > HIX_CRYPTO_AAD_MAX ||
	    req.in_len > HIX_CRYPTO_DATA_MAX || req.out_len < req.in_len ||
	    req.tag_len != HIX_CRYPTO_TAG_LEN)
		return -EINVAL;
	if (op != HIX_CRYPTO_CHACHA20 && op != HIX_CRYPTO_AEAD_SEAL &&
	    op != HIX_CRYPTO_AEAD_OPEN)
		return -EINVAL;
	if (req.in_len && !qubix_user_range_ok(req.out, req.in_len, true))
		return -EFAULT;
	if (op == HIX_CRYPTO_AEAD_SEAL &&
	    !qubix_user_range_ok(req.tag, HIX_CRYPTO_TAG_LEN, true))
		return -EFAULT;

	memcpy(key, req.key, sizeof(key));
	memcpy(nonce, req.nonce, sizeof(nonce));
	ret = copy_in(&aad, req.aad, req.aad_len);
	if (ret)
		goto out;
	if (op == HIX_CRYPTO_AEAD_OPEN) {
		if (!qubix_user_range_ok(req.tag, HIX_CRYPTO_TAG_LEN, false)) {
			ret = -EFAULT;
			goto out;
		}
		memcpy(tag, req.tag, sizeof(tag));
	}
	ret = copy_in(&in, req.in, req.in_len);
	if (ret)
		goto out;
	out = req.in_len ? kmalloc(req.in_len) : NULL;
	if (req.in_len && !out) {
		ret = -ENOMEM;
		goto out;
	}

	switch (op) {
	case HIX_CRYPTO_CHACHA20:
		if (req.in_len)
			memcpy(out, in, req.in_len);
		ret = hix_rust_chacha20_xor(key, nonce, req.flags, out,
					    req.in_len);
		break;
	case HIX_CRYPTO_AEAD_SEAL:
		ret = hix_rust_aead_seal(key, nonce, aad, req.aad_len, in,
					 req.in_len, out, tag);
		break;
	case HIX_CRYPTO_AEAD_OPEN:
		ret = hix_rust_aead_open(key, nonce, aad, req.aad_len, in,
					 req.in_len, out, tag);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (!ret && req.in_len)
		memcpy(req.out, out, req.in_len);
	if (!ret && op == HIX_CRYPTO_AEAD_SEAL)
		memcpy(req.tag, tag, sizeof(tag));
	if (!ret)
		req.result = req.in_len;

out:
	wipe(key, sizeof(key));
	wipe(nonce, sizeof(nonce));
	wipe(tag, sizeof(tag));
	if (aad) {
		wipe(aad, req.aad_len);
		kfree(aad);
	}
	if (in) {
		wipe(in, req.in_len);
		kfree(in);
	}
	if (out) {
		wipe(out, req.in_len);
		kfree(out);
	}
out_req:
	if (qubix_user_range_ok(ureq, sizeof(req), true))
		memcpy(ureq, &req, sizeof(req));
	return ret;
}
