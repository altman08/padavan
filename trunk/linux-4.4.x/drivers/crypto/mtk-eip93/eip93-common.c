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
#include <crypto/sha.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>

#include "eip93-cipher.h"
#include "eip93-hash.h"
#include "eip93-common.h"
#include "eip93-main.h"
#include "eip93-regs.h"

int eip93_parse_ctrl_stat_err(struct eip93_device *eip93, int err)
{
	u32 ext_err;

	if (!err)
		return 0;

	switch (err & ~EIP93_PE_CTRL_PE_EXT_ERR_CODE) {
	case EIP93_PE_CTRL_PE_AUTH_ERR:
	case EIP93_PE_CTRL_PE_PAD_ERR:
		return -EBADMSG;
	/* let software handle anti-replay errors */
	case EIP93_PE_CTRL_PE_SEQNUM_ERR:
		return 0;
	case EIP93_PE_CTRL_PE_EXT_ERR:
		break;
	default:
		dev_err(eip93->dev, "Unhandled error 0x%08x\n", err);
		return -EINVAL;
	}

	/* Parse additional ext errors */
	ext_err = FIELD_GET(EIP93_PE_CTRL_PE_EXT_ERR_CODE, err);
	switch (ext_err) {
	case EIP93_PE_CTRL_PE_EXT_ERR_BUS:
	case EIP93_PE_CTRL_PE_EXT_ERR_PROCESSING:
		return -EIO;
	case EIP93_PE_CTRL_PE_EXT_ERR_DESC_OWNER:
		return -EACCES;
	case EIP93_PE_CTRL_PE_EXT_ERR_INVALID_CRYPTO_OP:
	case EIP93_PE_CTRL_PE_EXT_ERR_INVALID_CRYPTO_ALGO:
	case EIP93_PE_CTRL_PE_EXT_ERR_SPI:
		return -EINVAL;
	case EIP93_PE_CTRL_PE_EXT_ERR_ZERO_LENGTH:
	case EIP93_PE_CTRL_PE_EXT_ERR_INVALID_PK_LENGTH:
	case EIP93_PE_CTRL_PE_EXT_ERR_BLOCK_SIZE_ERR:
		return -EBADMSG;
	default:
		dev_err(eip93->dev, "Unhandled ext error 0x%08x\n", ext_err);
		return -EINVAL;
	}
}

static void *eip93_ring_next_wptr(struct eip93_device *eip93, struct eip93_desc_ring *ring)
{
	void *ptr = READ_ONCE(ring->write);
	void *read = READ_ONCE(ring->read);

	if ((ptr == read - ring->offset) || (read == ring->base && ptr == ring->base_end))
		return ERR_PTR(-ENOMEM);

	if (ring->write == ring->base_end)
		ring->write = ring->base;
	else
		ring->write += ring->offset;

	return ptr;
}

static void *eip93_ring_next_rptr(struct eip93_device *eip93, struct eip93_desc_ring *ring)
{
	void *ptr = READ_ONCE(ring->read);

	if (READ_ONCE(ring->write) == ptr)
		return ERR_PTR(-ENOENT);

	if (ring->read == ring->base_end)
		ring->read = ring->base;
	else
		ring->read += ring->offset;

	return ptr;
}

int eip93_put_descriptor(struct eip93_device *eip93, struct eip93_descriptor *desc)
{
	struct eip93_descriptor *cdesc;
	struct eip93_descriptor *rdesc;

	rdesc = eip93_ring_next_wptr(eip93, &eip93->ring->rdr);
	if (IS_ERR(rdesc))
		return -ENOENT;

	cdesc = eip93_ring_next_wptr(eip93, &eip93->ring->cdr);
	if (IS_ERR(cdesc))
		return -ENOENT;

	memset(rdesc, 0, sizeof(struct eip93_descriptor));

	memcpy(cdesc, desc, sizeof(struct eip93_descriptor));
	dma_wmb();

	return 0;
}

void *eip93_get_descriptor(struct eip93_device *eip93)
{
	struct eip93_descriptor *cdesc;
	void *ptr;

	cdesc = eip93_ring_next_rptr(eip93, &eip93->ring->cdr);
	if (IS_ERR(cdesc))
		return ERR_PTR(-ENOENT);

	memset(cdesc, 0, sizeof(struct eip93_descriptor));

	ptr = eip93_ring_next_rptr(eip93, &eip93->ring->rdr);
	if (IS_ERR(ptr))
		return ERR_PTR(-ENOENT);

	return ptr;
}

static void eip93_free_sg_copy(const int len, struct scatterlist **sg)
{
	if (!*sg || !len)
		return;

	memzero_explicit(sg_virt(*sg), len);
	free_pages((unsigned long)sg_virt(*sg), get_order(len));
	kfree(*sg);
	*sg = NULL;
}

static int eip93_make_sg_copy(struct scatterlist *src, struct scatterlist **dst, const u32 len,
			      const bool copy)
{
	void *pages;

	*dst = kmalloc(sizeof(**dst), GFP_KERNEL);
	if (!*dst)
		return -ENOMEM;

	pages = (void *)__get_free_pages(GFP_KERNEL | GFP_DMA | __GFP_ZERO, get_order(len));
	if (!pages) {
		kfree(*dst);
		*dst = NULL;
		return -ENOMEM;
	}

	sg_init_table(*dst, 1);
	sg_set_buf(*dst, pages, len);

	/* copy only as requested */
	if (copy)
		sg_copy_to_buffer(src, sg_nents(src), pages, len);

	return 0;
}

static bool eip93_is_sg_aligned(struct scatterlist *sg, u32 len, const int blksize)
{
	int nents;

	for (nents = 0; sg; sg = sg_next(sg), ++nents) {
		if (!IS_ALIGNED(sg->offset, 4))
			return false;

		if (len <= sg->length) {
			if (!IS_ALIGNED(len, blksize))
				return false;

			return true;
		}

		if (!IS_ALIGNED(sg->length, blksize))
			return false;

		len -= sg->length;
	}
	return false;
}

int check_valid_request(struct eip93_cipher_reqctx *rctx)
{
	struct scatterlist *src = rctx->sg_src, *dst = rctx->sg_dst;
	u32 src_len, dst_len;
	int nsrc, ndst, ret;
	bool aligned;

	if (!IS_CTR(rctx->flags) && !IS_ALIGNED(rctx->textsize, rctx->blksize))
		return -EINVAL;
	if (rctx->assoclen > UINT_MAX - rctx->textsize - rctx->authsize)
		return -EOVERFLOW;
	src_len = dst_len = rctx->assoclen + rctx->textsize;
	if (IS_DECRYPT(rctx->flags))
		src_len += rctx->authsize;
	else
		dst_len += rctx->authsize;
	if (!src_len)
		return -EINVAL;
	nsrc = sg_nents_for_len(src, src_len);
	ndst = sg_nents_for_len(dst, dst_len);
	if (nsrc <= 0 || ndst <= 0)
		return -EINVAL;

	/* AEAD always uses private output: never expose unauthenticated plaintext. */
	aligned = !rctx->authsize && eip93_is_sg_aligned(src, src_len, rctx->blksize) &&
		  eip93_is_sg_aligned(dst, dst_len, rctx->blksize);
	if (!aligned) {
		rctx->copy_len = max(src_len, dst_len);
		ret = eip93_make_sg_copy(NULL, &rctx->sg_src, rctx->copy_len, false);
		if (ret)
			return ret;
		sg_copy_to_buffer(src, nsrc, sg_virt(rctx->sg_src), src_len);
		ret = eip93_make_sg_copy(NULL, &rctx->sg_dst, rctx->copy_len, false);
		if (ret) {
			eip93_free_sg_copy(rctx->copy_len, &rctx->sg_src);
			rctx->sg_src = src;
			return ret;
		}
	}
	rctx->src_nents = sg_nents_for_len(rctx->sg_src, src_len);
	rctx->dst_nents = sg_nents_for_len(rctx->sg_dst, dst_len);
	if (rctx->sg_src == rctx->sg_dst)
		rctx->src_nents = rctx->dst_nents = max(rctx->src_nents, rctx->dst_nents);
	return 0;
}

/*
 * Set sa_record function:
 * Even sa_record is set to "0", keep " = 0" for readability.
 */
void eip93_set_sa_record(struct sa_record *sa_record, const unsigned int keylen, const u32 flags)
{
	memset(sa_record, 0, sizeof(*sa_record));

	sa_record->sa_cmd0_word |= EIP93_SA_CMD_IV_FROM_STATE;
	if (!IS_ECB(flags))
		sa_record->sa_cmd0_word |= EIP93_SA_CMD_SAVE_IV;

	sa_record->sa_cmd0_word |= EIP93_SA_CMD_OP_BASIC;

	switch ((flags & EIP93_ALG_MASK)) {
	case EIP93_ALG_AES:
		sa_record->sa_cmd0_word |= EIP93_SA_CMD_CIPHER_AES;
		sa_record->sa_cmd1_word |= FIELD_PREP(EIP93_SA_CMD_AES_KEY_LENGTH, keylen >> 3);
		break;
	case EIP93_ALG_3DES:
		sa_record->sa_cmd0_word |= EIP93_SA_CMD_CIPHER_3DES;
		break;
	case EIP93_ALG_DES:
		sa_record->sa_cmd0_word |= EIP93_SA_CMD_CIPHER_DES;
		break;
	default:
		sa_record->sa_cmd0_word |= EIP93_SA_CMD_CIPHER_NULL;
	}

	switch ((flags & EIP93_HASH_MASK)) {
	case EIP93_HASH_SHA256:
		sa_record->sa_cmd0_word |= EIP93_SA_CMD_HASH_SHA256;
		break;
	case EIP93_HASH_SHA224:
		sa_record->sa_cmd0_word |= EIP93_SA_CMD_HASH_SHA224;
		break;
	case EIP93_HASH_SHA1:
		sa_record->sa_cmd0_word |= EIP93_SA_CMD_HASH_SHA1;
		break;
	case EIP93_HASH_MD5:
		sa_record->sa_cmd0_word |= EIP93_SA_CMD_HASH_MD5;
		break;
	default:
		sa_record->sa_cmd0_word |= EIP93_SA_CMD_HASH_NULL;
	}

	sa_record->sa_cmd0_word |= EIP93_SA_CMD_PAD_ZERO;

	switch ((flags & EIP93_MODE_MASK)) {
	case EIP93_MODE_CBC:
		sa_record->sa_cmd1_word |= EIP93_SA_CMD_CHIPER_MODE_CBC;
		break;
	case EIP93_MODE_CTR:
		sa_record->sa_cmd1_word |= EIP93_SA_CMD_CHIPER_MODE_CTR;
		break;
	case EIP93_MODE_ECB:
		sa_record->sa_cmd1_word |= EIP93_SA_CMD_CHIPER_MODE_ECB;
		break;
	}

	sa_record->sa_cmd0_word |= EIP93_SA_CMD_DIGEST_3WORD;
	if (IS_HASH(flags)) {
		sa_record->sa_cmd1_word |= EIP93_SA_CMD_COPY_PAD;
		sa_record->sa_cmd1_word |= EIP93_SA_CMD_COPY_DIGEST;
	}

	if (IS_HMAC(flags)) {
		sa_record->sa_cmd1_word |= EIP93_SA_CMD_HMAC;
		sa_record->sa_cmd1_word |= EIP93_SA_CMD_COPY_HEADER;
	}

	sa_record->sa_spi = 0x0;
	sa_record->sa_seqmum_mask[0] = 0xFFFFFFFF;
	sa_record->sa_seqmum_mask[1] = 0x0;
}

/*
 * Poor mans Scatter/gather function:
 * Create a Descriptor for every segment to avoid copying buffers.
 * For performance better to wait for hardware to perform multiple DMA
 */
static int eip93_scatter_combine(struct eip93_device *eip93, struct eip93_cipher_reqctx *rctx,
				 u32 datalen, u32 split, int offsetin)
{
	struct eip93_descriptor *cdesc = rctx->cdesc;
	struct scatterlist *sgsrc = rctx->sg_src;
	struct scatterlist *sgdst = rctx->sg_dst;
	unsigned int remainin = sg_dma_len(sgsrc);
	unsigned int remainout = sg_dma_len(sgdst);
	dma_addr_t saddr = sg_dma_address(sgsrc);
	dma_addr_t daddr = sg_dma_address(sgdst);
	dma_addr_t state_addr;
	u32 src_addr, dst_addr, len, n;
	bool nextin = false;
	bool nextout = false;
	int offsetout = 0;
	int err;
	unsigned long deadline = jiffies + 10 * HZ;

	if (IS_ECB(rctx->flags))
		rctx->sa_state_base = 0;

	if (split < datalen) {
		state_addr = rctx->sa_state_ctr_base;
		n = split;
	} else {
		state_addr = rctx->sa_state_base;
		n = datalen;
	}

	do {
		if (nextin) {
			sgsrc = sg_next(sgsrc);
			remainin = sg_dma_len(sgsrc);
			if (remainin == 0)
				continue;

			saddr = sg_dma_address(sgsrc);
			offsetin = 0;
			nextin = false;
		}

		if (nextout) {
			sgdst = sg_next(sgdst);
			remainout = sg_dma_len(sgdst);
			if (remainout == 0)
				continue;

			daddr = sg_dma_address(sgdst);
			offsetout = 0;
			nextout = false;
		}
		src_addr = saddr + offsetin;
		dst_addr = daddr + offsetout;

		/* Length is a 20-bit field; keep intermediate chunks block aligned. */
		len = min3(remainin, remainout, n);
		len = min_t(u32, len, EIP93_PE_LENGTH_LENGTH & ~(rctx->blksize - 1));
		remainin -= len;
		remainout -= len;
		offsetin += len;
		offsetout += len;
		nextin = !remainin;
		nextout = !remainout;
		n -= len;

		cdesc->src_addr = src_addr;
		cdesc->dst_addr = dst_addr;
		cdesc->state_addr = state_addr;
		cdesc->pe_length_word =
			FIELD_PREP(EIP93_PE_LENGTH_HOST_PE_READY, EIP93_PE_LENGTH_HOST_READY);
		cdesc->pe_length_word |= FIELD_PREP(EIP93_PE_LENGTH_LENGTH, len);

		if (n == 0) {
			n = datalen - split;
			split = datalen;
			state_addr = rctx->sa_state_base;
		}

		if (n == 0)
			cdesc->user_id |= FIELD_PREP(EIP93_PE_USER_ID_DESC_FLAGS, EIP93_DESC_LAST);

		/*
		 * Loop - Delay - No need to rollback
		 * Maybe refine by slowing down at EIP93_RING_BUSY
		 */
again:
		{
			unsigned long irqflags;

			spin_lock_irqsave(&eip93->ring->write_lock, irqflags);
			err = eip93_put_descriptor(eip93, cdesc);
			spin_unlock_irqrestore(&eip93->ring->write_lock, irqflags);
		}
		if (err) {
			if (time_after(jiffies, deadline)) {
				eip93_abort(eip93);
				return -ETIMEDOUT;
			}
			usleep_range(EIP93_RING_BUSY_DELAY, EIP93_RING_BUSY_DELAY * 2);
			goto again;
		}
		/* Writing new descriptor count starts DMA action */
		eip93->submitted = true;
		writel(1, eip93->base + EIP93_REG_PE_CD_COUNT);
	} while (n);

	return -EINPROGRESS;
}

int eip93_send_req(struct crypto_async_request *async, const u8 *reqiv,
		   struct eip93_cipher_reqctx *rctx)
{
	struct eip93_crypto_ctx *ctx = crypto_tfm_ctx(async->tfm);
	struct eip93_device *eip93 = ctx->eip93;
	struct scatterlist *src = rctx->sg_src;
	struct scatterlist *dst = rctx->sg_dst;
	struct sa_state *sa_state;
	struct eip93_descriptor cdesc = {};
	u32 flags = rctx->flags;
	int offsetin = 0, err;
	u32 datalen = rctx->assoclen + rctx->textsize;
	u32 split = datalen;
	u32 start, end, ctr, blocks;
	u32 iv[AES_BLOCK_SIZE / sizeof(u32)];
	int crypto_async_idr = -1;

	rctx->sa_state_ctr = NULL;
	rctx->sa_state = NULL;

	if (IS_ECB(flags))
		goto skip_iv;

	memcpy(iv, reqiv, rctx->ivsize);

	rctx->sa_state = kzalloc(sizeof(*rctx->sa_state), GFP_KERNEL);
	if (!rctx->sa_state)
		return -ENOMEM;

	sa_state = rctx->sa_state;

	memcpy(sa_state->state_iv, iv, rctx->ivsize);
	if (IS_RFC3686(flags)) {
		sa_state->state_iv[0] = ctx->sa_nonce;
		sa_state->state_iv[1] = iv[0];
		sa_state->state_iv[2] = iv[1];
		sa_state->state_iv[3] = (u32 __force)cpu_to_be32(0x1);
	} else if (!IS_HMAC(flags) && IS_CTR(flags)) {
		/* Compute data length. */
		blocks = DIV_ROUND_UP(rctx->textsize, AES_BLOCK_SIZE);
		ctr = be32_to_cpu((__be32 __force)iv[3]);
		/* Check 32bit counter overflow. */
		start = ctr;
		end = start + blocks - 1;
		if (end < start) {
			split = AES_BLOCK_SIZE * -start;
			/*
			 * Increment the counter manually to cope with
			 * the hardware counter overflow.
			 */
			iv[3] = 0xffffffff;
			crypto_inc((u8 *)iv, AES_BLOCK_SIZE);

			rctx->sa_state_ctr = kzalloc(sizeof(*rctx->sa_state_ctr), GFP_KERNEL);
			if (!rctx->sa_state_ctr) {
				err = -ENOMEM;
				goto fail;
			}

			memcpy(rctx->sa_state_ctr->state_iv, reqiv, rctx->ivsize);
			memcpy(sa_state->state_iv, iv, rctx->ivsize);

			rctx->sa_state_ctr_base =
				dma_map_single(eip93->dev, rctx->sa_state_ctr,
					       sizeof(*rctx->sa_state_ctr), DMA_BIDIRECTIONAL);
			err = dma_mapping_error(eip93->dev, rctx->sa_state_ctr_base);
			if (err) {
				err = -ENOMEM;
				goto fail;
			}
			rctx->state_ctr_mapped = true;
		}
	}

	rctx->sa_state_base = dma_map_single(eip93->dev, rctx->sa_state, sizeof(*rctx->sa_state),
					     DMA_BIDIRECTIONAL);
	err = dma_mapping_error(eip93->dev, rctx->sa_state_base);
	if (err) {
		err = -ENOMEM;
		goto fail;
	}
	rctx->state_mapped = true;

skip_iv:

	cdesc.pe_ctrl_stat_word =
		FIELD_PREP(EIP93_PE_CTRL_PE_READY_DES_TRING_OWN, EIP93_PE_CTRL_HOST_READY);
	cdesc.sa_addr = rctx->sa_record_base;
	cdesc.arc4_addr = 0;

	{
		spin_lock_bh(&eip93->ring->idr_lock);
		crypto_async_idr = idr_alloc(&eip93->ring->crypto_async_idr, async, 0,
					     EIP93_RING_NUM - 1, GFP_ATOMIC);
		spin_unlock_bh(&eip93->ring->idr_lock);
	}

	if (crypto_async_idr < 0) {
		err = crypto_async_idr;
		goto fail;
	}

	cdesc.user_id = FIELD_PREP(EIP93_PE_USER_ID_CRYPTO_IDR, (u16)crypto_async_idr) |
			FIELD_PREP(EIP93_PE_USER_ID_DESC_FLAGS, rctx->desc_flags);

	rctx->cdesc = &cdesc;

	/* map DMA_BIDIRECTIONAL to invalidate cache on destination
	 * implies __dma_cache_wback_inv
	 */
	if (!dma_map_sg(eip93->dev, dst, rctx->dst_nents, DMA_BIDIRECTIONAL)) {
		err = -ENOMEM;
		goto fail;
	}

	rctx->dst_mapped = true;

	if (src != dst && !dma_map_sg(eip93->dev, src, rctx->src_nents, DMA_TO_DEVICE)) {
		err = -ENOMEM;
		goto fail;
	}

	rctx->src_mapped = src != dst;
	err = eip93_scatter_combine(eip93, rctx, datalen, split, offsetin);
	if (err == -EINPROGRESS)
		return err;

fail:
	if (crypto_async_idr >= 0) {
		spin_lock_bh(&eip93->ring->idr_lock);
		idr_remove(&eip93->ring->crypto_async_idr, crypto_async_idr);
		spin_unlock_bh(&eip93->ring->idr_lock);
	}
	return err;
}

void eip93_unmap_dma(struct eip93_device *eip93, struct eip93_cipher_reqctx *rctx,
		     struct scatterlist *reqsrc, struct scatterlist *reqdst, int err)
{
	u32 len = rctx->assoclen + rctx->textsize;
	u32 authsize = IS_ENCRYPT(rctx->flags) ? rctx->authsize : 0;
	int i;

	if (rctx->src_mapped)
		dma_unmap_sg(eip93->dev, rctx->sg_src, rctx->src_nents, DMA_TO_DEVICE);
	if (rctx->dst_mapped)
		dma_unmap_sg(eip93->dev, rctx->sg_dst, rctx->dst_nents, DMA_BIDIRECTIONAL);
	if (!err && authsize && !IS_HASH_MD5(rctx->flags)) {
		u8 *tag = sg_virt(rctx->sg_dst) + len;

		for (i = 0; i < authsize; i += sizeof(u32)) {
			u32 word;

			memcpy(&word, tag + i, sizeof(word));
			word = be32_to_cpu((__be32 __force)word);
			memcpy(tag + i, &word, sizeof(word));
		}
	}
	if (rctx->sg_dst != reqdst) {
		if (!err)
			sg_copy_from_buffer(reqdst, sg_nents(reqdst), sg_virt(rctx->sg_dst),
					    len + authsize);
		eip93_free_sg_copy(rctx->copy_len, &rctx->sg_dst);
	}
	if (rctx->sg_src != reqsrc)
		eip93_free_sg_copy(rctx->copy_len, &rctx->sg_src);
}

void eip93_handle_result(struct eip93_device *eip93, struct eip93_cipher_reqctx *rctx, u8 *reqiv)
{
	if (rctx->state_ctr_mapped)
		dma_unmap_single(eip93->dev, rctx->sa_state_ctr_base, sizeof(*rctx->sa_state_ctr),
				 DMA_BIDIRECTIONAL);
	if (rctx->state_mapped)
		dma_unmap_single(eip93->dev, rctx->sa_state_base, sizeof(*rctx->sa_state),
				 DMA_BIDIRECTIONAL);
	if (reqiv && IS_CTR(rctx->flags) && !IS_RFC3686(rctx->flags)) {
		u32 carry = DIV_ROUND_UP(rctx->textsize, AES_BLOCK_SIZE);
		int i;

		for (i = AES_BLOCK_SIZE - 1; i >= 0; i--) {
			carry += reqiv[i];
			reqiv[i] = carry;
			carry >>= 8;
		}
	} else if (reqiv && IS_CBC(rctx->flags)) {
		memcpy(reqiv, rctx->sa_state->state_iv, rctx->ivsize);
	}
	kzfree(rctx->sa_state_ctr);
	kzfree(rctx->sa_state);
}

int eip93_hmac_setkey(u32 ctx_flags, const u8 *key, unsigned int keylen, unsigned int hashlen,
		      u8 *dest_ipad, u8 *dest_opad, bool skip_ipad)
{
	u8 ipad[SHA256_BLOCK_SIZE], opad[SHA256_BLOCK_SIZE];
	struct crypto_ahash *ahash_tfm;
	struct eip93_hash_reqctx *rctx;
	struct ahash_request *req;
	DECLARE_CRYPTO_WAIT(wait);
	struct scatterlist sg[1];
	const char *alg_name;
	int i, ret;

	switch (ctx_flags & EIP93_HASH_MASK) {
	case EIP93_HASH_SHA256:
		alg_name = "sha256-eip93";
		break;
	case EIP93_HASH_SHA224:
		alg_name = "sha224-eip93";
		break;
	case EIP93_HASH_SHA1:
		alg_name = "sha1-eip93";
		break;
	case EIP93_HASH_MD5:
		alg_name = "md5-eip93";
		break;
	default: /* Impossible */
		return -EINVAL;
	}

	ahash_tfm = crypto_alloc_ahash(alg_name, 0, 0);
	if (IS_ERR(ahash_tfm))
		return PTR_ERR(ahash_tfm);

	req = ahash_request_alloc(ahash_tfm, GFP_ATOMIC);
	if (!req) {
		ret = -ENOMEM;
		goto err_ahash;
	}

	rctx = ahash_request_ctx(req);
	crypto_init_wait(&wait);
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG, crypto_req_done, &wait);

	/* Hash the key if > SHA256_BLOCK_SIZE */
	if (keylen > SHA256_BLOCK_SIZE) {
		sg_init_one(&sg[0], key, keylen);

		ahash_request_set_crypt(req, sg, ipad, keylen);
		ret = crypto_wait_req(crypto_ahash_digest(req), &wait);
		if (ret)
			goto err_req;

		keylen = hashlen;
	} else {
		memcpy(ipad, key, keylen);
	}

	/* Copy to opad */
	memset(ipad + keylen, 0, SHA256_BLOCK_SIZE - keylen);
	memcpy(opad, ipad, SHA256_BLOCK_SIZE);

	/* Pad with HMAC constants */
	for (i = 0; i < SHA256_BLOCK_SIZE; i++) {
		ipad[i] ^= HMAC_IPAD_VALUE;
		opad[i] ^= HMAC_OPAD_VALUE;
	}

	if (skip_ipad) {
		memcpy(dest_ipad, ipad, SHA256_BLOCK_SIZE);
	} else {
		/* Hash ipad */
		sg_init_one(&sg[0], ipad, SHA256_BLOCK_SIZE);
		ahash_request_set_crypt(req, sg, dest_ipad, SHA256_BLOCK_SIZE);
		ret = crypto_ahash_init(req);
		if (ret)
			goto err_req;

		/* Disable HASH_FINALIZE for ipad hash */
		rctx->partial_hash = true;

		ret = crypto_wait_req(crypto_ahash_finup(req), &wait);
		if (ret)
			goto err_req;
	}

	/* Hash opad */
	sg_init_one(&sg[0], opad, SHA256_BLOCK_SIZE);
	ahash_request_set_crypt(req, sg, dest_opad, SHA256_BLOCK_SIZE);
	ret = crypto_ahash_init(req);
	if (ret)
		goto err_req;

	/* Disable HASH_FINALIZE for opad hash */
	rctx->partial_hash = true;

	ret = crypto_wait_req(crypto_ahash_finup(req), &wait);
	if (ret)
		goto err_req;

	if (!IS_HASH_MD5(ctx_flags)) {
		for (i = 0; i < SHA256_DIGEST_SIZE / sizeof(u32); i++) {
			u32 *ipad_hash = (u32 *)dest_ipad;
			u32 *opad_hash = (u32 *)dest_opad;

			if (!skip_ipad)
				ipad_hash[i] = (u32 __force)cpu_to_be32(ipad_hash[i]);
			opad_hash[i] = (u32 __force)cpu_to_be32(opad_hash[i]);
		}
	}

err_req:
	memzero_explicit(ipad, sizeof(ipad));
	memzero_explicit(opad, sizeof(opad));
	ahash_request_free(req);
err_ahash:
	crypto_free_ahash(ahash_tfm);

	return ret;
}
