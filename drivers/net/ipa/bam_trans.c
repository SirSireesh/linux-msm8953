// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */

#include <linux/types.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>

#include "bam.h"
#include "bam_trans.h"
#include "ipa_trans.h"
#include "ipa_gsi.h"

int bam_channel_trans_init(struct bam *bam, u32 channel_id)
{
	struct ipa_channel *channel = &bam->base.channel[channel_id];
	struct ipa_trans_info *trans_info;
	int ret;

	trans_info = &channel->trans_info;

	ret = ipa_trans_pool_init(&trans_info->pool, sizeof(struct ipa_trans),
			BAM_MAX_BURST_SIZE, BAM_MAX_BURST_SIZE);
	if (ret)
		return ret;

	/* FIXME: find out actual BAM hardware limits */
	ret = ipa_trans_pool_init(&trans_info->sg_pool,
				  sizeof(struct scatterlist),
				  BAM_MAX_BURST_SIZE, BAM_MAX_BURST_SIZE);
	if (ret)
		goto err_trans_pool_exit;

	spin_lock_init(&trans_info->spinlock);
	INIT_LIST_HEAD(&trans_info->alloc);
	INIT_LIST_HEAD(&trans_info->pending);
	INIT_LIST_HEAD(&trans_info->complete);
	INIT_LIST_HEAD(&trans_info->polled);

	return 0;

err_trans_pool_exit:
	ipa_trans_pool_exit(&trans_info->pool);
	return ret;
}

/* Allocate a GSI transaction on a channel */
struct ipa_trans *bam_channel_trans_alloc(struct bam *bam, u32 channel_id,
					  u32 tre_count,
					  enum dma_data_direction direction)
{
	struct ipa_channel *channel = &bam->base.channel[channel_id];
	struct ipa_trans_info *trans_info;
	struct ipa_trans *trans;

	/* assert(tre_count <= gsi_channel_trans_tre_max(gsi, channel_id)); */

	trans_info = &channel->trans_info;

	/* Allocate and initialize non-zero fields in the the transaction */
	trans = ipa_trans_pool_alloc(&trans_info->pool, 1);
	trans->transport = &bam->base;
	trans->channel_id = channel_id;
	trans->tre_count = tre_count;
	init_completion(&trans->completion);

	/* Allocate the scatterlist and (if requested) info entries. */
	trans->sgl = ipa_trans_pool_alloc(&trans_info->sg_pool, tre_count);
	sg_init_marker(trans->sgl, tre_count);

	trans->direction = direction;

	spin_lock_bh(&trans_info->spinlock);

	list_add_tail(&trans->links, &trans_info->alloc);

	spin_unlock_bh(&trans_info->spinlock);

	refcount_set(&trans->refcount, 1);

	return trans;
}

void bam_trans_callback(void *arg)
{
	struct ipa_trans *trans = arg;
	pr_info("ipa: in cb\n");
	/* If the entire SGL was mapped when added, unmap it now */
	if (trans->direction != DMA_NONE)
		dma_unmap_sg(trans->transport->dev, trans->sgl, trans->used,
				trans->direction);

	/* FIXME
	 * On downstream, the length of the DMA is got from the BAM HW descriptor
	 * On mainline, this is not yet supported (the BAM driver  needs a
	 * dma_metadata_client implementation. Until I implement that,
	 * I'm hardcoding 8128 here, which is the size of the buffer we share with
	 * the IPA hardware for each packet. This could mean potentially invalid
	 * packets would be parsed and created, so this should be fixed ASAP
	 */
	trans->len = 8128;

	ipa_gsi_trans_complete(trans);

	complete(&trans->completion);

	ipa_trans_free(trans);
}

void __bam_trans_commit(struct ipa_trans *trans)
{
	struct ipa_channel *channel = &trans->transport->channel[trans->channel_id];
	struct bam_channel_priv *priv = channel->priv;
	enum ipa_cmd_opcode opcode = IPA_CMD_NONE;
	struct ipa_cmd_info *info;
	struct scatterlist *sg;
	u32 byte_count = 0;
	u32 i;
	enum dma_transfer_direction direction;

	if (channel->toward_ipa)
		direction = DMA_MEM_TO_DEV;
	else
		direction = DMA_DEV_TO_MEM;

	/* assert(trans->used > 0); */

	info = trans->info ? &trans->info[0] : NULL;
	for_each_sg(trans->sgl, sg, trans->used, i) {
		bool last_tre = i == trans->used - 1;
		dma_addr_t addr = sg_dma_address(sg);
		u32 len = sg_dma_len(sg);
		u32 dma_flags = 0;
		struct dma_async_tx_descriptor *desc;

		byte_count += len;
		if (info)
			opcode = info++->opcode;

		if (opcode != IPA_CMD_NONE) {
			pr_info("ipa: prepping imm cmd\n");
			len = opcode;
			dma_flags |= DMA_PREP_IMM_CMD;
		}

		if (last_tre) {
			pr_info("ipa: req irq\n");
			dma_flags |= DMA_PREP_INTERRUPT;
		}

		desc = dmaengine_prep_slave_single(priv->chan, addr, len,
				direction, dma_flags);

		if (last_tre) {
			desc->callback = bam_trans_callback;
			desc->callback_param = trans;
		}

		desc->cookie = dmaengine_submit(desc);

		if (last_tre)
			trans->cookie = desc->cookie;
	}

	if (channel->toward_ipa) {
		/* We record TX bytes when they are sent */
		trans->len = byte_count;
		trans->trans_count = channel->trans_count;
		trans->byte_count = channel->byte_count;
		channel->trans_count++;
		channel->byte_count += byte_count;
	}

	ipa_trans_move_pending(trans);

	dma_async_issue_pending(priv->chan);
}

void bam_trans_commit(struct ipa_trans *trans)
{
	if (trans->used)
		__bam_trans_commit(trans);
	else
		ipa_trans_free(trans);
}

void bam_trans_commit_wait(struct ipa_trans *trans)
{
	if (!trans->used)
		goto out_trans_free;

	refcount_inc(&trans->refcount);

	__bam_trans_commit(trans);

	wait_for_completion(&trans->completion);

out_trans_free:
	ipa_trans_free(trans);
}

int bam_trans_commit_wait_timeout(struct ipa_trans *trans,
				  unsigned long timeout)
{
	unsigned long timeout_jiffies = msecs_to_jiffies(timeout);
	unsigned long remaining = 1;	/* In case of empty transaction */

	if (!trans->used)
		goto out_trans_free;

	refcount_inc(&trans->refcount);

	__bam_trans_commit(trans);

	remaining = wait_for_completion_timeout(&trans->completion,
			timeout_jiffies);

out_trans_free:
	ipa_trans_free(trans);

	return remaining ? 0 : -ETIMEDOUT;
}
