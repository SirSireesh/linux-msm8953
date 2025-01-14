/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */
#ifndef _IPA_TRANS_H_
#define _IPA_TRANS_H_

#include <linux/types.h>
#include <linux/refcount.h>
#include <linux/completion.h>
#include <linux/dma-direction.h>
#include <linux/dmaengine.h>

#include "sps_trans.h"
#include "gsi_trans.h"

struct scatterlist;

struct ipa_trans {
	struct list_head links;		/* (gsi/sps)_channel lists */

	/*FIXME: change to a union */
	struct gsi *gsi;
	struct sps *sps;

	u8 channel_id;

	bool cancelled;			/* true if transaction was cancelled */

	u8 tre_count;			/* # TREs requested, only used for gsi */
	u8 used;			/* # entries used in sgl[] */
	u32 len;			/* total # bytes across sgl[] */

	void *data;
	struct scatterlist *sgl;
	struct ipa_cmd_info *info;	/* array of entries, or null */
	enum dma_data_direction direction;
	dma_cookie_t cookie;

	refcount_t refcount;
	struct completion completion;

	u64 byte_count;			/* channel byte_count when committed */
	u64 trans_count;		/* channel trans_count when committed */
};

/**
 * ipa_trans_pool_init() - Initialize a pool of structures for transactions
 * @gsi:	GSI pointer
 * @size:	Size of elements in the pool
 * @count:	Minimum number of elements in the pool
 * @max_alloc:	Maximum number of elements allocated at a time from pool
 *
 * Return:	0 if successful, or a negative error code
 */
int ipa_trans_pool_init(struct ipa_trans_pool *pool, size_t size, u32 count,
			   u32 max_alloc);

/**
 * ipa_trans_pool_alloc() - Allocate one or more elements from a pool
 * @pool:	Pool pointer
 * @count:	Number of elements to allocate from the pool
 *
 * Return:	Virtual address of element(s) allocated from the pool
 */
void *ipa_trans_pool_alloc(struct ipa_trans_pool *pool, u32 count);

/**
 * ipa_trans_pool_exit() - Inverse of ipa_trans_pool_init()
 * @pool:	Pool pointer
 */
void ipa_trans_pool_exit(struct ipa_trans_pool *pool);

/**
 * ipa_trans_pool_init_dma() - Initialize a pool of DMA-able structures
 * @dev:	Device used for DMA
 * @pool:	Pool pointer
 * @size:	Size of elements in the pool
 * @count:	Minimum number of elements in the pool
 * @max_alloc:	Maximum number of elements allocated at a time from pool
 *
 * Return:	0 if successful, or a negative error code
 *
 * Structures in this pool reside in DMA-coherent memory.
 */
int ipa_trans_pool_init_dma(struct device *dev, struct ipa_trans_pool *pool,
			    size_t size, u32 count, u32 max_alloc);

/**
 * ipa_trans_pool_alloc_dma() - Allocate an element from a DMA pool
 * @pool:	DMA pool pointer
 * @addr:	DMA address "handle" associated with the allocation
 *
 * Return:	Virtual address of element allocated from the pool
 *
 * Only one element at a time may be allocated from a DMA pool.
 */
void *ipa_trans_pool_alloc_dma(struct ipa_trans_pool *pool, dma_addr_t *addr);

/**
 * ipa_trans_pool_exit() - Inverse of ipa_trans_pool_init()
 * @pool:	Pool pointer
 */
void ipa_trans_pool_exit_dma(struct device *dev, struct ipa_trans_pool *pool);

/**
 * ipa_channel_trans_alloc() - Allocate a GSI transaction on a channel
 * @ipa:	GSI pointer
 * @channel_id:	Channel the transaction is associated with
 * @tre_count:	Number of elements in the transaction
 * @direction:	DMA direction for entire SGL (or DMA_NONE)
 *
 * Return:	A GSI transaction structure, or a null pointer if all
 *		available transactions are in use
 */
struct ipa_trans *ipa_channel_trans_alloc(struct ipa *ipa, u32 channel_id,
					  u32 tre_count,
					  enum dma_data_direction direction);

/**
 * ipa_trans_free() - Free a previously-allocated GSI transaction
 * @trans:	Transaction to be freed
 */
void ipa_trans_free(struct ipa_trans *trans);

/**
 * ipa_trans_cmd_add() - Add an immediate command to a transaction
 * @trans:	Transaction
 * @buf:	Buffer pointer for command payload
 * @size:	Number of bytes in buffer
 * @addr:	DMA address for payload
 * @direction:	Direction of DMA transfer (or DMA_NONE if none required)
 * @opcode:	IPA immediate command opcode
 */
void ipa_trans_cmd_add(struct ipa_trans *trans, void *buf, u32 size,
		       dma_addr_t addr, enum dma_data_direction direction,
		       enum ipa_cmd_opcode opcode);

/**
 * ipa_trans_page_add() - Add a page transfer to a transaction
 * @trans:	Transaction
 * @page:	Page pointer
 * @size:	Number of bytes (starting at offset) to transfer
 * @offset:	Offset within page for start of transfer
 */
int ipa_trans_page_add(struct ipa_trans *trans, struct page *page, u32 size,
		       u32 offset);

/**
 * ipa_trans_skb_add() - Add a socket transfer to a transaction
 * @trans:	Transaction
 * @skb:	Socket buffer for transfer (outbound)
 *
 * Return:	0, or -EMSGSIZE if socket data won't fit in transaction.
 */
int ipa_trans_skb_add(struct ipa_trans *trans, struct sk_buff *skb);

/**
 * ipa_trans_commit() - Commit a GSI transaction
 * @trans:	Transaction to commit
 * @ring_db:	Whether to tell the hardware about these queued transfers
 */
void ipa_trans_commit(struct ipa_trans *trans, bool ring_db);

/**
 * ipa_trans_commit_wait() - Commit a GSI transaction and wait for it
 *			     to complete
 * @trans:	Transaction to commit
 */
void ipa_trans_commit_wait(struct ipa_trans *trans);

/**
 * ipa_trans_commit_wait_timeout() - Commit a GSI transaction and wait for
 *				     it to complete, with timeout
 * @trans:	Transaction to commit
 * @timeout:	Timeout period (in milliseconds)
 */
int ipa_trans_commit_wait_timeout(struct ipa_trans *trans,
				  unsigned long timeout);

/**
 * ipa_trans_read_byte() - Issue a single byte read TRE on a channel
 * @ipa:	GSI pointer
 * @channel_id:	Channel on which to read a byte
 * @addr:	DMA address into which to transfer the one byte
 *
 * This is not a transaction operation at all.  It's defined here because
 * it needs to be done in coordination with other transaction activity.
 */
int ipa_trans_read_byte(struct ipa *ipa, u32 channel_id, dma_addr_t addr);

/**
 * ipa_trans_read_byte_done() - Clean up after a single byte read TRE
 * @ipa:	GSI pointer
 * @channel_id:	Channel on which byte was read
 *
 * This function needs to be called to signal that the work related
 * to reading a byte initiated by ipa_trans_read_byte() is complete.
 */
void ipa_trans_read_byte_done(struct ipa *ipa, u32 channel_id);

void ipa_trans_move_pending(struct ipa_trans *trans);

void ipa_trans_move_polled(struct ipa_trans *trans);

void ipa_trans_move_complete(struct ipa_trans *trans);

void ipa_trans_complete(struct ipa_trans *trans);
#endif /* _IPA_TRANS_H_ */
