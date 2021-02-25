/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2020, The Linux Foundation. All rights reserved. */
#ifndef _BAM_TRANS_H_
#define _BAM_TRANS_H_

#include <linux/types.h>
#include <linux/refcount.h>
#include <linux/completion.h>
#include <linux/dma-direction.h>

#include "ipa_cmd.h"

struct scatterlist;
struct device;
struct sk_buff;

struct bam;

int bam_channel_trans_init(struct bam *bam, u32 channel_id);

/**
 * bam_channel_trans_alloc() - Allocate a GSI transaction on a channel
 * @bam:	GSI pointer
 * @channel_id:	Channel the transaction is associated with
 * @tre_count:	Number of elements in the transaction
 * @direction:	DMA direction for entire SGL (or DMA_NONE)
 *
 * Return:	A GSI transaction structure, or a null pointer if all
 *		available transactions are in use
 */

struct ipa_trans *bam_channel_trans_alloc(struct bam *bam, u32 channel_id,
					  u32 tre_count,
					  enum dma_data_direction direction);

/**
 * bam_trans_free() - Free a previously-allocated GSI transaction
 * @trans:	Transaction to be freed
 */
void bam_trans_free(struct ipa_trans *trans);

/**
 * bam_trans_commit() - Commit a GSI transaction
 * @trans:	Transaction to commit
 */
void bam_trans_commit(struct ipa_trans *trans);

/**
 * bam_trans_commit_wait() - Commit a GSI transaction and wait for it
 *			     to complete
 * @trans:	Transaction to commit
 */
void bam_trans_commit_wait(struct ipa_trans *trans);

/**
 * bam_trans_commit_wait_timeout() - Commit a GSI transaction and wait for
 *				     it to complete, with timeout
 * @trans:	Transaction to commit
 * @timeout:	Timeout period (in milliseconds)
 */
int bam_trans_commit_wait_timeout(struct ipa_trans *trans,
				  unsigned long timeout);

#endif /* _BAM_TRANS_H_ */
