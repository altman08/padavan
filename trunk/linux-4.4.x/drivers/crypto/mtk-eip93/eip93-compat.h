/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _EIP93_COMPAT_H_
#define _EIP93_COMPAT_H_

#include <linux/crypto.h>
#include <crypto/aes.h>
#include <crypto/des.h>

#define HMAC_IPAD_VALUE 0x36
#define HMAC_OPAD_VALUE 0x5c

/* Fixed, contiguous hardware register masks; keep these helpers driver-local. */
#define FIELD_PREP(mask, val)                                                                      \
	((typeof(mask))(((typeof(mask))(val) << __builtin_ctzll(mask)) & (mask)))
#define FIELD_GET(mask, val) ((typeof(mask))(((val) & (mask)) >> __builtin_ctzll(mask)))

static inline int aes_check_keylen(unsigned int len)
{
	return len == AES_KEYSIZE_128 || len == AES_KEYSIZE_192 || len == AES_KEYSIZE_256 ? 0
											  : -EINVAL;
}

static inline int eip93_verify_des_key(struct crypto_tfm *tfm, const u8 *key, unsigned int len)
{
	u32 schedule[DES_EXPKEY_WORDS];
	int weak;

	if (len != DES_KEY_SIZE)
		return -EINVAL;
	weak = !des_ekey(schedule, key);
	memzero_explicit(schedule, sizeof(schedule));
	if (weak && (crypto_tfm_get_flags(tfm) & CRYPTO_TFM_REQ_WEAK_KEY)) {
		crypto_tfm_set_flags(tfm, CRYPTO_TFM_RES_WEAK_KEY);
		return -EINVAL;
	}
	return 0;
}

static inline int eip93_verify_des3_key(struct crypto_tfm *tfm, const u8 *key, unsigned int len)
{
	u32 schedule[DES3_EDE_EXPKEY_WORDS];
	u32 flags = crypto_tfm_get_flags(tfm);
	int ret;

	ret = __des3_ede_setkey(schedule, &flags, key, len);
	crypto_tfm_set_flags(tfm, flags & CRYPTO_TFM_RES_MASK);
	memzero_explicit(schedule, sizeof(schedule));
	return ret;
}

#endif
