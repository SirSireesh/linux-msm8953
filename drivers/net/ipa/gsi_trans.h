/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */
#ifndef _GSI_TRANS_H_
#define _GSI_TRANS_H_

#include <linux/types.h>
#include <linux/refcount.h>
#include <linux/completion.h>
#include <linux/dma-direction.h>

#include "gsi.h"
#include "ipa_cmd.h"

struct scatterlist;
struct device;
struct sk_buff;

struct gsi;
struct gsi_trans_pool;

/**
 * gsi_channel_trans_alloc() - Allocate a GSI transaction on a channel
 * @gsi:	GSI pointer
 * @channel_id:	Channel the transaction is associated with
 * @tre_count:	Number of elements in the transaction
 * @direction:	DMA direction for entire SGL (or DMA_NONE)
 *
 * Return:	A GSI transaction structure, or a null pointer if all
 *		available transactions are in use
 */
struct ipa_trans *gsi_channel_trans_alloc(struct gsi *gsi, u32 channel_id,
					  u32 tre_count,
					  enum dma_data_direction direction);

/**
 * gsi_trans_free() - Free a previously-allocated GSI transaction
 * @trans:	Transaction to be freed
 */
void gsi_trans_free(struct ipa_trans *trans);

/**
 * gsi_trans_commit() - Commit a GSI transaction
 * @trans:	Transaction to commit
 * @ring_db:	Whether to tell the hardware about these queued transfers
 */
void gsi_trans_commit(struct ipa_trans *trans, bool ring_db);

/**
 * gsi_trans_commit_wait() - Commit a GSI transaction and wait for it
 *			     to complete
 * @trans:	Transaction to commit
 */
void gsi_trans_commit_wait(struct ipa_trans *trans);

/**
 * gsi_trans_commit_wait_timeout() - Commit a GSI transaction and wait for
 *				     it to complete, with timeout
 * @trans:	Transaction to commit
 * @timeout:	Timeout period (in milliseconds)
 */
int gsi_trans_commit_wait_timeout(struct ipa_trans *trans,
				  unsigned long timeout);

/**
 * gsi_trans_read_byte() - Issue a single byte read TRE on a channel
 * @gsi:	GSI pointer
 * @channel_id:	Channel on which to read a byte
 * @addr:	DMA address into which to transfer the one byte
 *
 * This is not a transaction operation at all.  It's defined here because
 * it needs to be done in coordination with other transaction activity.
 */
int gsi_trans_read_byte(struct gsi *gsi, u32 channel_id, dma_addr_t addr);

/**
 * gsi_trans_read_byte_done() - Clean up after a single byte read TRE
 * @gsi:	GSI pointer
 * @channel_id:	Channel on which byte was read
 *
 * This function needs to be called to signal that the work related
 * to reading a byte initiated by gsi_trans_read_byte() is complete.
 */
void gsi_trans_read_byte_done(struct gsi *gsi, u32 channel_id);

void gsi_trans_tre_release(struct ipa_trans_info *trans_info, u32 tre_count);

#endif /* _GSI_TRANS_H_ */
