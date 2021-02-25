/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2020, The Linux Foundation. All rights reserved. */
#ifndef _BAM_H_
#define _BAM_H_

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>

#include "ipa_trans_info.h"
#include "ipa_transport.h"

/* Maximum number of channels supported by the driver */
#define BAM_CHANNEL_COUNT_MAX	20
#define BAM_MAX_BURST_SIZE	0x10

struct bam;
struct ipa_gsi_endpoint_data;

// TODO MOVE THIS TO PRIVATE
struct bam_channel_priv { // This is stored in ipa_channel.priv
	struct dma_chan *chan;
};

struct bam {
	struct ipa_transport base;
};

static inline struct bam *to_bam(struct ipa_transport *transport)
{
	return container_of(transport, struct bam, base);
}
// END PRIVATE

struct ipa_transport* bam_transport_init(struct platform_device *pdev,
	     struct ipa *ipa, u32 count,
	     const struct ipa_gsi_endpoint_data *data);

#endif /* _BAM_H_ */
