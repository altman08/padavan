/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 - 2021
 *
 * Richard van Schagen <vschagen@icloud.com>
 * Christian Marangi <ansuelsmth@gmail.com>
 */

#include <crypto/aes.h>
#include <crypto/ctr.h>
#include "eip93-compat.h"
#include <linux/dma-mapping.h>

#include "eip93-aes.h"
#include "eip93-cipher.h"
#include "eip93-common.h"
#include "eip93-des.h"
#include "eip93-regs.h"

void eip93_skcipher_handle_result(struct crypto_async_request *async, int err)
{
	struct eip93_crypto_ctx *ctx = crypto_tfm_ctx(async->tfm);
	struct eip93_device *eip93 = ctx->eip93;
	struct ablkcipher_request *req = ablkcipher_request_cast(async);
	struct eip93_cipher_reqctx *rctx = ablkcipher_request_ctx(req);

	eip93_unmap_dma(eip93, rctx, req->src, req->dst, err);
	eip93_handle_result(eip93, rctx, err ? NULL : req->info);
	dma_unmap_single(eip93->dev, rctx->sa_record_base, sizeof(*rctx->sa_record), DMA_TO_DEVICE);
	kzfree(rctx->sa_record);

	ablkcipher_request_complete(req, err);
}

static int eip93_skcipher_send_req(struct crypto_async_request *async)
{
	struct ablkcipher_request *req = ablkcipher_request_cast(async);
	struct eip93_cipher_reqctx *rctx = ablkcipher_request_ctx(req);
	int err;

	err = check_valid_request(rctx);

	if (err)
		return err;

	return eip93_send_req(async, req->info, rctx);
}

/* Crypto skcipher API functions */
static int eip93_skcipher_cra_init(struct crypto_tfm *tfm)
{
	struct eip93_crypto_ctx *ctx = crypto_tfm_ctx(tfm);
	struct eip93_alg_template *tmpl =
		container_of(tfm->__crt_alg, struct eip93_alg_template, alg.ablkcipher);

	tfm->crt_ablkcipher.reqsize = sizeof(struct eip93_cipher_reqctx);

	memset(ctx, 0, sizeof(*ctx));

	ctx->eip93 = tmpl->eip93;
	ctx->type = tmpl->type;

	ctx->sa_record = kzalloc(sizeof(*ctx->sa_record), GFP_KERNEL);
	if (!ctx->sa_record)
		return -ENOMEM;

	return 0;
}

static void eip93_skcipher_cra_exit(struct crypto_tfm *tfm)
{
	struct eip93_crypto_ctx *ctx = crypto_tfm_ctx(tfm);

	kzfree(ctx->sa_record);
}

static int eip93_skcipher_setkey(struct crypto_ablkcipher *ctfm, const u8 *key, unsigned int len)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(ctfm);
	struct eip93_crypto_ctx *ctx = crypto_tfm_ctx(tfm);
	struct eip93_alg_template *tmpl =
		container_of(tfm->__crt_alg, struct eip93_alg_template, alg.ablkcipher);
	struct sa_record *sa_record = ctx->sa_record;
	unsigned int keylen = len;
	u32 flags = tmpl->flags;
	u32 nonce = 0;
	int ret;

	if (!key || !keylen)
		return -EINVAL;

	if (IS_RFC3686(flags)) {
		if (len < CTR_RFC3686_NONCE_SIZE)
			return -EINVAL;

		keylen = len - CTR_RFC3686_NONCE_SIZE;
		memcpy(&nonce, key + keylen, CTR_RFC3686_NONCE_SIZE);
	}

	if (flags & EIP93_ALG_DES) {
		ctx->blksize = DES_BLOCK_SIZE;
		ret = eip93_verify_des_key(tfm, key, keylen);
		if (ret)
			return ret;
	}
	if (flags & EIP93_ALG_3DES) {
		ctx->blksize = DES3_EDE_BLOCK_SIZE;
		ret = eip93_verify_des3_key(tfm, key, keylen);
		if (ret)
			return ret;
	}

	if (flags & EIP93_ALG_AES) {
		ctx->blksize = AES_BLOCK_SIZE;
		ret = aes_check_keylen(keylen);
		if (ret)
			return ret;
	}

	eip93_set_sa_record(sa_record, keylen, flags);

	memcpy(sa_record->sa_key, key, keylen);
	ctx->sa_nonce = nonce;
	sa_record->sa_nonce = nonce;

	return 0;
}

static int eip93_skcipher_process(struct crypto_async_request *async)
{
	struct ablkcipher_request *req = ablkcipher_request_cast(async);
	struct eip93_cipher_reqctx *rctx = ablkcipher_request_ctx(req);
	struct eip93_crypto_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct crypto_ablkcipher *skcipher = crypto_ablkcipher_reqtfm(req);
	int ret;
	u16 flags = rctx->flags;

	memset(rctx, 0, sizeof(*rctx));
	rctx->flags = flags;

	if (!req->nbytes)
		return 0;

	/*
	 * ECB and CBC algorithms require message lengths to be
	 * multiples of block size.
	 */
	if (IS_ECB(rctx->flags) || IS_CBC(rctx->flags))
		if (!IS_ALIGNED(req->nbytes, crypto_ablkcipher_blocksize(skcipher)))
			return -EINVAL;

	rctx->sa_record = kmemdup(ctx->sa_record, sizeof(*ctx->sa_record), GFP_KERNEL);
	if (!rctx->sa_record)
		return -ENOMEM;
	if (IS_DECRYPT(rctx->flags))
		rctx->sa_record->sa_cmd0_word |= EIP93_SA_CMD_DIRECTION_IN;
	rctx->sa_record_base = dma_map_single(ctx->eip93->dev, rctx->sa_record,
					      sizeof(*rctx->sa_record), DMA_TO_DEVICE);
	if (dma_mapping_error(ctx->eip93->dev, rctx->sa_record_base)) {
		kzfree(rctx->sa_record);
		return -ENOMEM;
	}

	rctx->assoclen = 0;
	rctx->textsize = req->nbytes;
	rctx->authsize = 0;
	rctx->sg_src = req->src;
	rctx->sg_dst = req->dst;
	rctx->ivsize = crypto_ablkcipher_ivsize(skcipher);
	rctx->blksize = ctx->blksize;
	rctx->desc_flags = EIP93_DESC_SKCIPHER;

	ret = eip93_skcipher_send_req(async);
	if (ret != -EINPROGRESS) {
		eip93_unmap_dma(ctx->eip93, rctx, req->src, req->dst, ret);
		eip93_handle_result(ctx->eip93, rctx, NULL);
		dma_unmap_single(ctx->eip93->dev, rctx->sa_record_base, sizeof(*rctx->sa_record),
				 DMA_TO_DEVICE);
		kzfree(rctx->sa_record);
	}
	return ret;
}

static int eip93_skcipher_encrypt(struct ablkcipher_request *req)
{
	struct eip93_cipher_reqctx *rctx = ablkcipher_request_ctx(req);
	struct eip93_alg_template *tmpl =
		container_of(req->base.tfm->__crt_alg, struct eip93_alg_template, alg.ablkcipher);

	if (!req->nbytes)
		return 0;
	rctx->flags = tmpl->flags;
	rctx->flags |= EIP93_ENCRYPT;

	return eip93_queue(&req->base, eip93_skcipher_process);
}

static int eip93_skcipher_decrypt(struct ablkcipher_request *req)
{
	struct eip93_cipher_reqctx *rctx = ablkcipher_request_ctx(req);
	struct eip93_alg_template *tmpl =
		container_of(req->base.tfm->__crt_alg, struct eip93_alg_template, alg.ablkcipher);

	if (!req->nbytes)
		return 0;
	rctx->flags = tmpl->flags;
	rctx->flags |= EIP93_DECRYPT;

	return eip93_queue(&req->base, eip93_skcipher_process);
}

/* Available algorithms in this module */
struct eip93_alg_template eip93_alg_ecb_aes = {
	.type = EIP93_ALG_TYPE_SKCIPHER,
	.flags = EIP93_MODE_ECB | EIP93_ALG_AES,
	.alg.ablkcipher = {
		.cra_u.ablkcipher = {
			.setkey = eip93_skcipher_setkey,
			.encrypt = eip93_skcipher_encrypt,
			.decrypt = eip93_skcipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = 0,
		},
		.cra_name = "ecb(aes)",
		.cra_driver_name = "ecb(aes-eip93)",
		.cra_priority = EIP93_CRA_PRIORITY,
		.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK |
			CRYPTO_ALG_KERN_DRIVER_ONLY,
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
		.cra_alignmask = 0xf,
		.cra_init = eip93_skcipher_cra_init,
		.cra_exit = eip93_skcipher_cra_exit,
		.cra_type = &crypto_ablkcipher_type,
		.cra_module = THIS_MODULE,
	},
};

struct eip93_alg_template eip93_alg_cbc_aes = {
	.type = EIP93_ALG_TYPE_SKCIPHER,
	.flags = EIP93_MODE_CBC | EIP93_ALG_AES,
	.alg.ablkcipher = {
		.cra_u.ablkcipher = {
			.setkey = eip93_skcipher_setkey,
			.encrypt = eip93_skcipher_encrypt,
			.decrypt = eip93_skcipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
		},
		.cra_name = "cbc(aes)",
		.cra_driver_name = "cbc(aes-eip93)",
		.cra_priority = EIP93_CRA_PRIORITY,
		.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK |
			CRYPTO_ALG_KERN_DRIVER_ONLY,
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
		.cra_alignmask = 0xf,
		.cra_init = eip93_skcipher_cra_init,
		.cra_exit = eip93_skcipher_cra_exit,
		.cra_type = &crypto_ablkcipher_type,
		.cra_module = THIS_MODULE,
	},
};

struct eip93_alg_template eip93_alg_ctr_aes = {
	.type = EIP93_ALG_TYPE_SKCIPHER,
	.flags = EIP93_MODE_CTR | EIP93_ALG_AES,
	.alg.ablkcipher = {
		.cra_u.ablkcipher = {
			.setkey = eip93_skcipher_setkey,
			.encrypt = eip93_skcipher_encrypt,
			.decrypt = eip93_skcipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
		},
		.cra_name = "ctr(aes)",
		.cra_driver_name = "ctr(aes-eip93)",
		.cra_priority = EIP93_CRA_PRIORITY,
		.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK |
			CRYPTO_ALG_KERN_DRIVER_ONLY,
		.cra_blocksize = 1,
		.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
		.cra_alignmask = 0xf,
		.cra_init = eip93_skcipher_cra_init,
		.cra_exit = eip93_skcipher_cra_exit,
		.cra_type = &crypto_ablkcipher_type,
		.cra_module = THIS_MODULE,
	},
};

struct eip93_alg_template eip93_alg_rfc3686_aes = {
	.type = EIP93_ALG_TYPE_SKCIPHER,
	.flags = EIP93_MODE_CTR | EIP93_MODE_RFC3686 | EIP93_ALG_AES,
	.alg.ablkcipher = {
		.cra_u.ablkcipher = {
			.setkey = eip93_skcipher_setkey,
			.encrypt = eip93_skcipher_encrypt,
			.decrypt = eip93_skcipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE + CTR_RFC3686_NONCE_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE + CTR_RFC3686_NONCE_SIZE,
			.ivsize = CTR_RFC3686_IV_SIZE,
		},
		.cra_name = "rfc3686(ctr(aes))",
		.cra_driver_name = "rfc3686(ctr(aes-eip93))",
		.cra_priority = EIP93_CRA_PRIORITY,
		.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK |
			CRYPTO_ALG_KERN_DRIVER_ONLY,
		.cra_blocksize = 1,
		.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
		.cra_alignmask = 0xf,
		.cra_init = eip93_skcipher_cra_init,
		.cra_exit = eip93_skcipher_cra_exit,
		.cra_type = &crypto_ablkcipher_type,
		.cra_module = THIS_MODULE,
	},
};

struct eip93_alg_template eip93_alg_ecb_des = {
	.type = EIP93_ALG_TYPE_SKCIPHER,
	.flags = EIP93_MODE_ECB | EIP93_ALG_DES,
	.alg.ablkcipher = {
		.cra_u.ablkcipher = {
			.setkey = eip93_skcipher_setkey,
			.encrypt = eip93_skcipher_encrypt,
			.decrypt = eip93_skcipher_decrypt,
			.min_keysize = DES_KEY_SIZE,
			.max_keysize = DES_KEY_SIZE,
			.ivsize = 0,
		},
		.cra_name = "ecb(des)",
		.cra_driver_name = "ecb(des-eip93)",
		.cra_priority = EIP93_CRA_PRIORITY,
		.cra_flags =
		CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC | CRYPTO_ALG_KERN_DRIVER_ONLY,
		.cra_blocksize = DES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
		.cra_alignmask = 0,
		.cra_init = eip93_skcipher_cra_init,
		.cra_exit = eip93_skcipher_cra_exit,
		.cra_type = &crypto_ablkcipher_type,
		.cra_module = THIS_MODULE,
	},
};

struct eip93_alg_template eip93_alg_cbc_des = {
	.type = EIP93_ALG_TYPE_SKCIPHER,
	.flags = EIP93_MODE_CBC | EIP93_ALG_DES,
	.alg.ablkcipher = {
		.cra_u.ablkcipher = {
			.setkey = eip93_skcipher_setkey,
			.encrypt = eip93_skcipher_encrypt,
			.decrypt = eip93_skcipher_decrypt,
			.min_keysize = DES_KEY_SIZE,
			.max_keysize = DES_KEY_SIZE,
			.ivsize = DES_BLOCK_SIZE,
		},
		.cra_name = "cbc(des)",
		.cra_driver_name = "cbc(des-eip93)",
		.cra_priority = EIP93_CRA_PRIORITY,
		.cra_flags =
		CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC | CRYPTO_ALG_KERN_DRIVER_ONLY,
		.cra_blocksize = DES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
		.cra_alignmask = 0,
		.cra_init = eip93_skcipher_cra_init,
		.cra_exit = eip93_skcipher_cra_exit,
		.cra_type = &crypto_ablkcipher_type,
		.cra_module = THIS_MODULE,
	},
};

struct eip93_alg_template eip93_alg_ecb_des3_ede = {
	.type = EIP93_ALG_TYPE_SKCIPHER,
	.flags = EIP93_MODE_ECB | EIP93_ALG_3DES,
	.alg.ablkcipher = {
		.cra_u.ablkcipher = {
			.setkey = eip93_skcipher_setkey,
			.encrypt = eip93_skcipher_encrypt,
			.decrypt = eip93_skcipher_decrypt,
			.min_keysize = DES3_EDE_KEY_SIZE,
			.max_keysize = DES3_EDE_KEY_SIZE,
			.ivsize = 0,
		},
		.cra_name = "ecb(des3_ede)",
		.cra_driver_name = "ecb(des3_ede-eip93)",
		.cra_priority = EIP93_CRA_PRIORITY,
		.cra_flags =
		CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC | CRYPTO_ALG_KERN_DRIVER_ONLY,
		.cra_blocksize = DES3_EDE_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
		.cra_alignmask = 0,
		.cra_init = eip93_skcipher_cra_init,
		.cra_exit = eip93_skcipher_cra_exit,
		.cra_type = &crypto_ablkcipher_type,
		.cra_module = THIS_MODULE,
	},
};

struct eip93_alg_template eip93_alg_cbc_des3_ede = {
	.type = EIP93_ALG_TYPE_SKCIPHER,
	.flags = EIP93_MODE_CBC | EIP93_ALG_3DES,
	.alg.ablkcipher = {
		.cra_u.ablkcipher = {
			.setkey = eip93_skcipher_setkey,
			.encrypt = eip93_skcipher_encrypt,
			.decrypt = eip93_skcipher_decrypt,
			.min_keysize = DES3_EDE_KEY_SIZE,
			.max_keysize = DES3_EDE_KEY_SIZE,
			.ivsize = DES3_EDE_BLOCK_SIZE,
		},
		.cra_name = "cbc(des3_ede)",
		.cra_driver_name = "cbc(des3_ede-eip93)",
		.cra_priority = EIP93_CRA_PRIORITY,
		.cra_flags =
		CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC | CRYPTO_ALG_KERN_DRIVER_ONLY,
		.cra_blocksize = DES3_EDE_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
		.cra_alignmask = 0,
		.cra_init = eip93_skcipher_cra_init,
		.cra_exit = eip93_skcipher_cra_exit,
		.cra_type = &crypto_ablkcipher_type,
		.cra_module = THIS_MODULE,
	},
};
