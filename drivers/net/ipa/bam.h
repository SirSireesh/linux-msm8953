/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2020, The Linux Foundation. All rights reserved. */
#ifndef _SPI_H_
#define _SPI_H_

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>

#include "ipa_trans_info.h"

/* Maximum number of channels supported by the driver */
#define SPS_CHANNEL_COUNT_MAX	20
#define SPS_MAX_BURST_SIZE	0x10

struct bam;
struct ipa_gsi_endpoint_data;

/* Execution Environment ID, same as GSI EE IDs */
enum bam_ee_id {
	SPS_EE_AP	= 0,
	SPS_EE_MODEM	= 1,
	SPS_EE_UC	= 2,
};

struct bam_channel {
	struct bam *bam;
	bool toward_ipa;
	bool command;			/* AP command TX channel or not */

	struct dma_chan *chan;

	struct completion completion;	/* signals channel command completion */

	u64 byte_count;			/* total # bytes transferred */
	u64 trans_count;		/* total # transactions */
	/* The following counts are used only for TX endpoints */
	u64 queued_byte_count;		/* last reported queued byte count */
	u64 queued_trans_count;		/* ...and queued trans count */
	u64 compl_byte_count;		/* last reported completed byte count */
	u64 compl_trans_count;		/* ...and completed trans count */

	struct ipa_trans_info trans_info;

	struct napi_struct napi;
};

struct bam {
	struct device *dev;	/* Same as IPA device */
	struct net_device dummy_dev; /* needed for NAPI */
	struct bam_channel channel[SPS_CHANNEL_COUNT_MAX];
	struct mutex mutex;
};

/**
 * bam_setup() - Set up the BAM DMA subsytem
 * @bam:	Address of SPS structure embedded in an IPA structure
 *
 * Return:	0
 *
 * Setup is a nop as the hardware is handled by the BAM DMA driver
 */
int bam_setup(struct bam *bam);

void bam_teardown(struct bam *bam);

int bam_init(struct bam *bam, struct platform_device *pdev, u32 count,
		const struct ipa_gsi_endpoint_data *data);

void bam_exit(struct bam *bam);

#endif /* _SPI_H_ */
