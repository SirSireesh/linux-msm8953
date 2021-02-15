// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */

#include <linux/types.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/refcount.h>
#include <linux/scatterlist.h>
#include <linux/dma-direction.h>

#include "ipa.h"
#include "gsi.h"
#include "bam.h"
#include "bam_trans.h"
#include "gsi_private.h"
#include "gsi_trans.h"
#include "ipa_gsi.h"
#include "ipa_data.h"
#include "ipa_cmd.h"
#include "ipa_trans.h"

/**
 * DOC: GSI Transactions
 *
 * A GSI transaction abstracts the behavior of a GSI channel by representing
 * everything about a related group of IPA commands in a single structure.
 * (A "command" in this sense is either a data transfer or an IPA immediate
 * command.)  Most details of interaction with the GSI hardware are managed
 * by the GSI transaction core, allowing users to simply describe commands
 * to be performed.  When a transaction has completed a callback function
 * (dependent on the type of endpoint associated with the channel) allows
 * cleanup of resources associated with the transaction.
 *
 * To perform a command (or set of them), a user of the GSI transaction
 * interface allocates a transaction, indicating the number of TREs required
 * (one per command).  If sufficient TREs are available, they are reserved
 * for use in the transaction and the allocation succeeds.  This way
 * exhaustion of the available TREs in a channel ring is detected
 * as early as possible.  All resources required to complete a transaction
 * are allocated at transaction allocation time.
 *
 * Commands performed as part of a transaction are represented in an array
 * of Linux scatterlist structures.  This array is allocated with the
 * transaction, and its entries are initialized using standard scatterlist
 * functions (such as sg_set_buf() or skb_to_sgvec()).
 *
 * Once a transaction's scatterlist structures have been initialized, the
 * transaction is committed.  The caller is responsible for mapping buffers
 * for DMA if necessary, and this should be done *before* allocating
 * the transaction.  Between a successful allocation and commit of a
 * transaction no errors should occur.
 *
 * Committing transfers ownership of the entire transaction to the GSI
 * transaction core.  The GSI transaction code formats the content of
 * the scatterlist array into the channel ring buffer and informs the
 * hardware that new TREs are available to process.
 *
 * The last TRE in each transaction is marked to interrupt the AP when the
 * GSI hardware has completed it.  Because transfers described by TREs are
 * performed strictly in order, signaling the completion of just the last
 * TRE in the transaction is sufficient to indicate the full transaction
 * is complete.
 *
 * When a transaction is complete, ipa_gsi_trans_complete() is called by the
 * GSI code into the IPA layer, allowing it to perform any final cleanup
 * required before the transaction is freed.
 */

/*TODO: make a "struct xfer_ops"*/
int ipa_trans_pool_init(struct ipa_trans_pool *pool, size_t size, u32 count,
			u32 max_alloc)
{
	void *virt;

#ifdef IPA_VALIDATE
	if (!size || size % 8)
		return -EINVAL;
	if (count < max_alloc)
		return -EINVAL;
	if (!max_alloc)
		return -EINVAL;
#endif /* IPA_VALIDATE */

	/* By allocating a few extra entries in our pool (one less
	 * than the maximum number that will be requested in a
	 * single allocation), we can always satisfy requests without
	 * ever worrying about straddling the end of the pool array.
	 * If there aren't enough entries starting at the free index,
	 * we just allocate free entries from the beginning of the pool.
	 */
	virt = kcalloc(count + max_alloc - 1, size, GFP_KERNEL);
	if (!virt)
		return -ENOMEM;

	pool->base = virt;
	/* If the allocator gave us any extra memory, use it */
	pool->count = ksize(pool->base) / size;
	pool->free = 0;
	pool->max_alloc = max_alloc;
	pool->size = size;
	pool->addr = 0;		/* Only used for DMA pools */

	return 0;
}

void ipa_trans_pool_exit(struct ipa_trans_pool *pool)
{
	kfree(pool->base);
	memset(pool, 0, sizeof(*pool));
}

/* Allocate the requested number of (zeroed) entries from the pool */
/* Home-grown DMA pool.  This way we can preallocate and use the tre_count
 * to guarantee allocations will succeed.  Even though we specify max_alloc
 * (and it can be more than one), we only allow allocation of a single
 * element from a DMA pool.
 */
int ipa_trans_pool_init_dma(struct device *dev, struct ipa_trans_pool *pool,
			    size_t size, u32 count, u32 max_alloc)
{
	size_t total_size;
	dma_addr_t addr;
	void *virt;

#ifdef IPA_VALIDATE
	if (!size || size % 8)
		return -EINVAL;
	if (count < max_alloc)
		return -EINVAL;
	if (!max_alloc)
		return -EINVAL;
#endif /* IPA_VALIDATE */

	/* Don't let allocations cross a power-of-two boundary */
	size = __roundup_pow_of_two(size);
	total_size = (count + max_alloc - 1) * size;

	/* The allocator will give us a power-of-2 number of pages.  But we
	 * can't guarantee that, so request it.  That way we won't waste any
	 * memory that would be available beyond the required space.
	 */
	total_size = get_order(total_size) << PAGE_SHIFT;

	virt = dma_alloc_coherent(dev, total_size, &addr, GFP_KERNEL);
	if (!virt)
		return -ENOMEM;

	pool->base = virt;
	pool->count = total_size / size;
	pool->free = 0;
	pool->size = size;
	pool->max_alloc = max_alloc;
	pool->addr = addr;

	return 0;
}

void ipa_trans_pool_exit_dma(struct device *dev, struct ipa_trans_pool *pool)
{
	dma_free_coherent(dev, pool->size, pool->base, pool->addr);
	memset(pool, 0, sizeof(*pool));
}

/* Return the byte offset of the next free entry in the pool */
static u32 ipa_trans_pool_alloc_common(struct ipa_trans_pool *pool, u32 count)
{
	u32 offset;

	/* assert(count > 0); */
	/* assert(count <= pool->max_alloc); */

	/* Allocate from beginning if wrap would occur */
	if (count > pool->count - pool->free)
		pool->free = 0;

	offset = pool->free * pool->size;
	pool->free += count;
	memset(pool->base + offset, 0, count * pool->size);

	return offset;
}

/* Allocate a contiguous block of zeroed entries from a pool */
void *ipa_trans_pool_alloc(struct ipa_trans_pool *pool, u32 count)
{
	return pool->base + ipa_trans_pool_alloc_common(pool, count);
}

/* Allocate a single zeroed entry from a DMA pool */
void *ipa_trans_pool_alloc_dma(struct ipa_trans_pool *pool, dma_addr_t *addr)
{
	u32 offset = ipa_trans_pool_alloc_common(pool, 1);

	*addr = pool->addr + offset;

	return pool->base + offset;
}

/* Return the pool element that immediately follows the one given.
 * This only works done if elements are allocated one at a time.
 */
void *ipa_trans_pool_next(struct ipa_trans_pool *pool, void *element)
{
	void *end = pool->base + pool->count * pool->size;

	/* assert(element >= pool->base); */
	/* assert(element < end); */
	/* assert(pool->max_alloc == 1); */
	element += pool->size;

	return element < end ? element : pool->base;
}

/* Return the oldest completed transaction for a channel (or null) */
struct ipa_trans *ipa_channel_trans_complete(struct gsi_channel *channel)
{
	/*FIXME: */
	return list_first_entry_or_null(&channel->trans_info.complete,
					struct ipa_trans, links);
}

/* Move a transaction from the allocated list to the pending list */
void ipa_trans_move_pending(struct ipa_trans *trans)
{
	struct ipa_trans_info *trans_info;

	if (trans->gsi)
		trans_info = &trans->gsi->channel[trans->channel_id].trans_info;
	else if (trans->bam)
		trans_info = &trans->bam->channel[trans->channel_id].trans_info;

	spin_lock_bh(&trans_info->spinlock);

	list_move_tail(&trans->links, &trans_info->pending);

	spin_unlock_bh(&trans_info->spinlock);
}

/* Move a transaction and all of its predecessors from the pending list
 * to the completed list.
 */
void ipa_trans_move_complete(struct ipa_trans *trans)
{
	struct ipa_trans_info *trans_info;
	struct list_head list;

	if (trans->gsi)
		trans_info = &trans->gsi->channel[trans->channel_id].trans_info;
	else if (trans->bam)
		trans_info = &trans->bam->channel[trans->channel_id].trans_info;

	spin_lock_bh(&trans_info->spinlock);

	/* Move this transaction and all predecessors to completed list */
	list_cut_position(&list, &trans_info->pending, &trans->links);
	list_splice_tail(&list, &trans_info->complete);

	spin_unlock_bh(&trans_info->spinlock);
}

/* Move a transaction from the completed list to the polled list */
void ipa_trans_move_polled(struct ipa_trans *trans)
{
	struct ipa_trans_info *trans_info;

	if (trans->gsi)
		trans_info = &trans->gsi->channel[trans->channel_id].trans_info;
	else if (trans->bam)
		trans_info = &trans->bam->channel[trans->channel_id].trans_info;

	spin_lock_bh(&trans_info->spinlock);

	list_move_tail(&trans->links, &trans_info->polled);

	spin_unlock_bh(&trans_info->spinlock);
}

/* Allocate a GSI transaction on a channel */
struct ipa_trans *ipa_channel_trans_alloc(struct ipa *ipa, u32 channel_id,
					  u32 tre_count,
					  enum dma_data_direction direction)
{
	if (ipa->version == IPA_VERSION_2_6L)
		return bam_channel_trans_alloc(&ipa->bam, channel_id,
				tre_count, direction);
	return gsi_channel_trans_alloc(&ipa->gsi, channel_id,
			tre_count, direction);
}

/* Free a previously-allocated transaction */
void ipa_trans_free(struct ipa_trans *trans)
{
	refcount_t *refcount = &trans->refcount;
	struct ipa_trans_info *trans_info;
	bool last;

	/* We must hold the lock to release the last reference */
	if (refcount_dec_not_one(refcount))
		return;

	if (trans->gsi)
		trans_info = &trans->gsi->channel[trans->channel_id].trans_info;
	else
		trans_info = &trans->bam->channel[trans->channel_id].trans_info;

	spin_lock_bh(&trans_info->spinlock);

	/* Reference might have been added before we got the lock */
	last = refcount_dec_and_test(refcount);
	if (last)
		list_del(&trans->links);

	spin_unlock_bh(&trans_info->spinlock);

	if (!last)
		return;

	ipa_gsi_trans_release(trans);

	/* Releasing the reserved TREs implicitly frees the sgl[] and
	 * (if present) info[] arrays, plus the transaction itself.
	 */
	if (trans->gsi)
		gsi_trans_tre_release(trans_info, trans->tre_count);
}

/* Add an immediate command to a transaction */
void ipa_trans_cmd_add(struct ipa_trans *trans, void *buf, u32 size,
		       dma_addr_t addr, enum dma_data_direction direction,
		       enum ipa_cmd_opcode opcode)
{
	struct ipa_cmd_info *info;
	u32 which = trans->used++;
	struct scatterlist *sg;

	/* assert(which < trans->tre_count); */

	/* Commands are quite different from data transfer requests.
	 * Their payloads come from a pool whose memory is allocated
	 * using dma_alloc_coherent().  We therefore do *not* map them
	 * for DMA (unlike what we do for pages and skbs).
	 *
	 * When a transaction completes, the SGL is normally unmapped.
	 * A command transaction has direction DMA_NONE, which tells
	 * gsi_trans_complete() to skip the unmapping step.
	 *
	 * The only things we use directly in a command scatter/gather
	 * entry are the DMA address and length.  We still need the SG
	 * table flags to be maintained though, so assign a NULL page
	 * pointer for that purpose.
	 */
	sg = &trans->sgl[which];
	sg_assign_page(sg, NULL);
	sg_dma_address(sg) = addr;
	sg_dma_len(sg) = size;

	info = &trans->info[which];
	info->opcode = opcode;
	info->direction = direction;
}

/* Add a page transfer to a transaction.  It will fill the only TRE. */
int ipa_trans_page_add(struct ipa_trans *trans, struct page *page, u32 size,
		       u32 offset)
{
	struct scatterlist *sg = &trans->sgl[0];
	int ret;

	/* assert(trans->tre_count == 1); */
	/* assert(!trans->used); */

	sg_set_page(sg, page, size, offset);
	if (trans->gsi)
		ret = dma_map_sg(trans->gsi->dev, sg, 1, trans->direction);
	else
		ret = dma_map_sg(trans->bam->dev, sg, 1, trans->direction);
	if (!ret)
		return -ENOMEM;

	trans->used++;	/* Transaction now owns the (DMA mapped) page */

	return 0;
}

/* Add an SKB transfer to a transaction. */
int ipa_trans_skb_add(struct ipa_trans *trans, struct sk_buff *skb)
{
	struct scatterlist *sg = &trans->sgl[0];
	u32 used;
	int ret;

	/* assert(trans->tre_count == 1); */
	/* assert(!trans->used); */

	/* skb->len will not be 0 (checked early) */
	ret = skb_to_sgvec(skb, sg, 0, skb->len);
	if (ret < 0)
		return ret;
	used = ret;

	if (trans->gsi)
		ret = dma_map_sg(trans->gsi->dev, sg, 1, trans->direction);
	else
		ret = dma_map_sg(trans->bam->dev, sg, 1, trans->direction);
	if (!ret)
		return -ENOMEM;

	trans->used += used;	/* Transaction now owns the (DMA mapped) skb */

	return 0;
}

/* Commit a transaction */
void ipa_trans_commit(struct ipa_trans *trans, bool ring_db)
{
	if (trans->gsi)
		gsi_trans_commit(trans, ring_db);
	else
		bam_trans_commit(trans);
}

/* Commit a GSI transaction and wait for it to complete */
void ipa_trans_commit_wait(struct ipa_trans *trans)
{
	if (trans->gsi)
		gsi_trans_commit_wait(trans);
	else
		bam_trans_commit_wait(trans);
}

/* Commit a GSI transaction and wait for it to complete, with timeout */
int ipa_trans_commit_wait_timeout(struct ipa_trans *trans,
				  unsigned long timeout)
{
	u32 ret = 0;

	if (trans->gsi)
		ret = gsi_trans_commit_wait_timeout(trans, timeout);
	else
		ret = bam_trans_commit_wait_timeout(trans, timeout);

	return ret;
}

/* Process the completion of a transaction; called while polling */
void ipa_trans_complete(struct ipa_trans *trans)
{
	/* If the entire SGL was mapped when added, unmap it now */
	if (trans->direction != DMA_NONE)
		dma_unmap_sg(trans->gsi->dev, trans->sgl, trans->used,
			     trans->direction);

	//FIXME: rename/refactor
	ipa_gsi_trans_complete(trans);

	complete(&trans->completion);

	ipa_trans_free(trans);
}

/* Cancel a channel's pending transactions */
void ipa_channel_trans_cancel_pending(struct gsi_channel *gsi,
		struct bam_channel *bam)
{
	struct ipa_trans_info *trans_info = gsi ? &gsi->trans_info: &bam->trans_info;
	struct ipa_trans *trans;
	bool cancelled;
	if (gsi)
		trans_info = &gsi->trans_info;
	else
		trans_info = &bam->trans_info;

	/* channel->gsi->mutex is held by caller */
	spin_lock_bh(&trans_info->spinlock);

	cancelled = !list_empty(&trans_info->pending);
	list_for_each_entry(trans, &trans_info->pending, links)
		trans->cancelled = true;

	list_splice_tail_init(&trans_info->pending, &trans_info->complete);

	spin_unlock_bh(&trans_info->spinlock);

	/* Schedule NAPI polling to complete the cancelled transactions */
	if (cancelled)
		napi_schedule(gsi ? &gsi->napi : &bam->napi);
}
