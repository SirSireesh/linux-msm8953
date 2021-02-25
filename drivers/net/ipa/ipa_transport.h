/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2020 Linaro Ltd.
 */

#ifndef _IPA_TRANSPORT_H_
#define _IPA_TRANSPORT_H_

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>

#include "ipa_version.h"
#include "ipa_trans_info.h"

#define IPA_CHANNEL_COUNT_MAX	20

/* We only care about channels between IPA and AP */
struct ipa;
struct ipa_transport;
struct ipa_channel {
	struct ipa_transport *transport;
	bool toward_ipa;
	bool command;			/* AP command TX channel or not */

	struct completion completion;	/* signals channel command completion */

	u64 byte_count;			/* total # bytes transferred */
	u64 trans_count;		/* total # transactions */
	/* The following counts are used only for TX endpoints */
	u64 queued_byte_count;		/* last reported queued byte count */
	u64 queued_trans_count;		/* ...and queued trans count */
	u64 compl_byte_count;		/* last reported completed byte count */
	u64 compl_trans_count;		/* ...and completed trans count */

	void *priv;			/* Transport-specific data */

	struct ipa_trans_info trans_info;

	struct napi_struct napi;
};

struct ipa_transport {
	struct device *dev;	/* Same as IPA device */
	struct ipa *ipa;
	enum ipa_version version;
	struct net_device dummy_dev; /* needed for NAPI */
	struct ipa_channel channel[IPA_CHANNEL_COUNT_MAX];
	u32 channel_count;
	struct mutex mutex;		/* protects commands, programming */

	/**
	 * gsi_setup() - Set up the transport
	 * @transport:	IPA transport to initialize
	 *
	 * Return:	0 if successful, or a negative error code
	 *
	 * Performs initialization that must wait until the hardware is
	 * ready (including firmware loaded).
	 */
	int (*setup)(struct ipa_transport *transport);
	/**
	 * teardown() - Tear down IPA transport
	 * @transport:	IPA transport previously passed to a successful
	 * transport_setup() call
	 */
	void (*teardown)(struct ipa_transport *transport);
	/**
	 * gsi_exit() - Deinitialize transport releasing all resources
	 * @gsi:	transport pointer 
	 */
	void (*exit)(struct ipa_transport *transport);

	u32 (*channel_tre_max)(struct ipa_transport *transport, u32 channel_id);
	u32 (*channel_trans_tre_max)(struct ipa_transport *transport, u32 channel_id);
	int (*channel_start)(struct ipa_transport *transport, u32 channel_id);
	int (*channel_stop)(struct ipa_transport *transport, u32 channel_id);
	void (*channel_reset)(struct ipa_transport *transport, u32 channel_id, bool doorbell);
	int (*channel_suspend)(struct ipa_transport *transport, u32 channel_id, bool stop);
	int (*channel_resume)(struct ipa_transport *transport, u32 channel_id, bool start);
};

static inline int ipa_transport_setup(struct ipa_transport *transport)
{
	return transport->setup(transport);
}

static inline void ipa_transport_teardown(struct ipa_transport *transport)
{
	transport->teardown(transport);
}

// FIXME Add checks for optional

/**
 * ipa_channel_tre_max() - Channel maximum number of in-flight TREs
 * @transport:	GSI pointer
 * @channel_id:	Channel whose limit is to be returned
 *
 * Return:	 The maximum number of TREs oustanding on the channel
 */
static inline u32 ipa_channel_tre_max(struct ipa_transport *transport, u32 channel_id)
{
	return transport->channel_tre_max(transport, channel_id);
}

/**
 * ipa_channel_trans_tre_max() - Maximum TREs in a single transaction
 * @transport:	GSI pointer
 * @channel_id:	Channel whose limit is to be returned
 *
 * Return:	 The maximum TRE count per transaction on the channel
 */
static inline u32 ipa_channel_trans_tre_max(struct ipa_transport *transport, u32 channel_id)
{
	return transport->channel_trans_tre_max(transport, channel_id);
}

/**
 * ipa_channel_start() - Start an allocated GSI channel
 * @transport:	GSI pointer
 * @channel_id:	Channel to start
 *
 * Return:	0 if successful, or a negative error code
 */
static inline int ipa_channel_start(struct ipa_transport *transport, u32 channel_id)
{
	return transport->channel_start(transport, channel_id);
}

/**
 * ipa_channel_stop() - Stop a started GSI channel
 * @transport:	GSI pointer returned by ipa_setup()
 * @channel_id:	Channel to stop
 *
 * Return:	0 if successful, or a negative error code
 */
static inline int ipa_channel_stop(struct ipa_transport *transport, u32 channel_id)
{
	return transport->channel_stop(transport, channel_id);
}

/**
 * ipa_channel_reset() - Reset an allocated GSI channel
 * @transport:	GSI pointer
 * @channel_id:	Channel to be reset
 * @doorbell:	Whether to (possibly) enable the doorbell engine
 *
 * Reset a channel and reconfigure it.  The @doorbell flag indicates
 * that the doorbell engine should be enabled if needed.
 *
 * GSI hardware relinquishes ownership of all pending receive buffer
 * transactions and they will complete with their cancelled flag set.
 */
static inline void ipa_channel_reset(struct ipa_transport *transport, u32 channel_id, bool doorbell)
{
	transport->channel_reset(transport, channel_id, doorbell);
}

static inline int ipa_channel_suspend(struct ipa_transport *transport, u32 channel_id, bool stop)
{
	return transport->channel_suspend(transport, channel_id, stop);
}
static inline int ipa_channel_resume(struct ipa_transport *transport, u32 channel_id, bool start)
{
	return transport->channel_resume(transport, channel_id, start);
}

static inline void ipa_transport_exit(struct ipa_transport *transport)
{
	transport->exit(transport);
}

#endif /* _IPA_TRANSPORT_H_ */
