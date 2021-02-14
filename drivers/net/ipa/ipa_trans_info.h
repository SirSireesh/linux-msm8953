/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef _IPA_TRANS_INFO_H_
#define _IPA_TRANS_INFO_H_

/*FIXME move to a common header */
struct ipa_trans_pool {
	void *base;			/* base address of element pool */
	u32 count;			/* # elements in the pool */
	u32 free;			/* next free element in pool (modulo) */
	u32 size;			/* size (bytes) of an element */
	u32 max_alloc;			/* max allocation request */
	dma_addr_t addr;		/* DMA address if DMA pool (or 0) */
};

struct ipa_trans_info {
	atomic_t tre_avail;		/* TREs available for allocation */
	struct ipa_trans_pool pool;	/* transaction pool */
	struct ipa_trans_pool sg_pool;	/* scatterlist pool */
	struct ipa_trans_pool cmd_pool;	/* command payload DMA pool */
	struct ipa_trans_pool info_pool;/* command information pool */
	struct ipa_trans **map;		/* TRE -> transaction map */

	spinlock_t spinlock;		/* protects updates to the lists */
	struct list_head alloc;		/* allocated, not committed */
	struct list_head pending;	/* committed, awaiting completion */
	struct list_head complete;	/* completed, awaiting poll */
	struct list_head polled;	/* returned by gsi_channel_poll_one() */
};

#endif /* _IPA_TRANS_INFO_H_ */
