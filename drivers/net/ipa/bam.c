// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/types.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/dma/qcom_bam_dma.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/io.h>
#include <linux/bug.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>

#include "ipa.h"
#include "bam.h"
#include "ipa_gsi.h"
#include "bam_trans.h"
#include "ipa_trans.h"
#include "ipa_data.h"

/**
 * DOC: The IPA Smart Peripheral System Interface
 *
 * The Smart Peripheral System is a means to communicate over BAM pipes to
 * the IPA block. The Modem also uses BAM pipes to communicate with the IPA
 * core.
 *
 * Refer the GSI documentation, because BAM is a precursor to GSI and more or less
 * the same, conceptually (maybe, IDK, I have no docs to go through).
 *
 * Each channel here corresponds to 1 BAM pipe configured in BAM2BAM mode
 *
 * IPA cmds are transferred one at a time, each in one BAM transfer.
 */

/* Get and configure the BAM DMA channel */
int bam_channel_init_one(struct bam *bam,
			 const struct ipa_gsi_endpoint_data *data, bool command)
{
	struct dma_slave_config bam_config;
	u32 channel_id = data->channel_id;
	struct ipa_channel *channel = &bam->base.channel[channel_id];
	struct bam_channel_priv *priv;
	int ret;

	/*TODO: if (!bam_channel_data_valid(bam, data))
		return -EINVAL;*/

	priv = devm_kzalloc(bam->base.dev, sizeof(struct bam_channel_priv),
			    GFP_KERNEL);
	if (!priv) {
		dev_err(bam->base.dev, "failed to allocate for BAM channels\n");
		return ENOMEM;
	}

	channel->priv = priv;

	priv->chan = dma_request_chan(bam->base.dev, data->channel_name);
	if (IS_ERR(priv->chan)) {
		dev_err(bam->base.dev, "failed to request BAM channel %s: %d\n",
				data->channel_name,
				(int) PTR_ERR(priv->chan));
		return PTR_ERR(priv->chan);
	}

	ret = bam_channel_trans_init(bam, data->channel_id);
	if (ret)
		goto err_dma_chan_free;

	if (data->toward_ipa) {
		bam_config.direction = DMA_MEM_TO_DEV;
		bam_config.dst_maxburst = BAM_MAX_BURST_SIZE;
	} else {
		bam_config.direction = DMA_DEV_TO_MEM;
		bam_config.src_maxburst = BAM_MAX_BURST_SIZE;
	}

	channel->toward_ipa = data->toward_ipa;

	dmaengine_slave_config(priv->chan, &bam_config);

	if (command)
		ret = ipa_cmd_pool_init(bam->base.dev, &channel->trans_info, 256, 20);

	if (!ret)
		return 0;

err_dma_chan_free:
	dma_release_channel(priv->chan);
	return ret;
}

static void bam_channel_exit_one(struct ipa_channel *channel)
{
	struct bam_channel_priv *priv = channel->priv;

	if (!priv)
		return;

	dmaengine_terminate_sync(priv->chan);
	dma_release_channel(priv->chan);
}

/* Get channels from BAM_DMA */
int bam_channel_init(struct bam *bam, u32 count,
		const struct ipa_gsi_endpoint_data *data)
{
	int ret = 0;
	u32 i;

	for (i = 0; i < count; ++i) {
		bool command = i == IPA_ENDPOINT_AP_COMMAND_TX;

		if (!data[i].channel_name || data[i].ee_id == GSI_EE_MODEM)
			continue;

		ret = bam_channel_init_one(bam, &data[i], command);
		if (ret)
			goto err_unwind;
	}

	return ret;

err_unwind:
	while (i--) {
		if (ipa_gsi_endpoint_data_empty(&data[i]))
			continue;

		bam_channel_exit_one(&bam->base.channel[i]);
	}
	return ret;
}

/* Inverse of bam_channel_init() */
void bam_channel_exit(struct bam *bam)
{
	u32 channel_id = BAM_CHANNEL_COUNT_MAX - 1;

	do
		bam_channel_exit_one(&bam->base.channel[channel_id]);
	while (channel_id--);
}

/* Inverse of bam_init() */
static void bam_exit(struct ipa_transport *transport)
{
	mutex_destroy(&transport->mutex);
	bam_channel_exit(to_bam(transport));
}

/* Return the oldest completed transaction for a channel (or null) */
struct ipa_trans *bam_channel_trans_complete(struct ipa_channel *channel)
{
	return list_first_entry_or_null(&channel->trans_info.complete,
					struct ipa_trans, links);
}

/* Return the channel id associated with a given channel */
static u32 bam_channel_id(struct ipa_channel *channel)
{
	return channel - &channel->transport->channel[0];
}

static void
bam_channel_tx_update(struct ipa_channel *channel, struct ipa_trans *trans)
{
	u64 byte_count = trans->byte_count + trans->len;
	u64 trans_count = trans->trans_count + 1;

	byte_count -= channel->compl_byte_count;
	channel->compl_byte_count += byte_count;
	trans_count -= channel->compl_trans_count;
	channel->compl_trans_count += trans_count;

	ipa_transport_channel_tx_completed(channel->transport, bam_channel_id(channel),
					   trans_count, byte_count);
}

static void
bam_channel_rx_update(struct ipa_channel *channel, struct ipa_trans *trans)
{
	/* FIXME */
	u64 byte_count = trans->byte_count + trans->len;

	channel->byte_count += byte_count;
	channel->trans_count++;
}

/* Consult hardware, move any newly completed transactions to completed list */
static void bam_channel_update(struct ipa_channel *channel)
{
	struct bam_channel_priv *priv = channel->priv;
	struct ipa_trans *trans;

	list_for_each_entry(trans, &channel->trans_info.pending, links) {
		enum dma_status trans_status =
				dma_async_is_tx_complete(priv->chan,
					trans->cookie, NULL, NULL);
		if (trans_status == DMA_COMPLETE)
			break;
	}
	/* Get the transaction for the latest completed event.  Take a
	 * reference to keep it from completing before we give the events
	 * for this and previous transactions back to the hardware.
	 */
	refcount_inc(&trans->refcount);

	/* For RX channels, update each completed transaction with the number
	 * of bytes that were actually received.  For TX channels, report
	 * the number of transactions and bytes this completion represents
	 * up the network stack.
	 */
	if (channel->toward_ipa)
		bam_channel_tx_update(channel, trans);
	else
		bam_channel_rx_update(channel, trans);

	ipa_trans_move_complete(trans);

	ipa_trans_free(trans);
}

/**
 * bam_channel_poll_one() - Return a single completed transaction on a channel
 * @channel:	Channel to be polled
 *
 * Return:	Transaction pointer, or null if none are available
 *
 * This function returns the first entry on a channel's completed transaction
 * list.  If that list is empty, the hardware is consulted to determine
 * whether any new transactions have completed.  If so, they're moved to the
 * completed list and the new first entry is returned.  If there are no more
 * completed transactions, a null pointer is returned.
 */
static struct ipa_trans *bam_channel_poll_one(struct ipa_channel *channel)
{
	struct ipa_trans *trans;

	/* Get the first transaction from the completed list */
	trans = bam_channel_trans_complete(channel);
	if (!trans) {
		bam_channel_update(channel);
		trans = bam_channel_trans_complete(channel);
	}

	if (trans)
		ipa_trans_move_polled(trans);

	return trans;
}

/**
 * bam_channel_poll() - NAPI poll function for a channel
 * @napi:	NAPI structure for the channel
 * @budget:	Budget supplied by NAPI core
 *
 * Return:	Number of items polled (<= budget)
 *
 * Single transactions completed by hardware are polled until either
 * the budget is exhausted, or there are no more.  Each transaction
 * polled is passed to ipa_trans_complete(), to perform remaining
 * completion processing and retire/free the transaction.
 */
static int bam_channel_poll(struct napi_struct *napi, int budget)
{
	struct ipa_channel *channel;
	int count = 0;

	channel = container_of(napi, struct ipa_channel, napi);
	while (count < budget) {
		struct ipa_trans *trans;

		count++;
		trans = bam_channel_poll_one(channel);
		if (!trans)
			break;
		ipa_trans_complete(trans);
	}

	if (count < budget)
		napi_complete(&channel->napi);

	return count;
}

/* Setup function for a single channel */
static void bam_channel_setup_one(struct bam *bam, u32 channel_id)
{
	struct ipa_channel *channel = &bam->base.channel[channel_id];

	if (!channel->transport)
		return;	/* Ignore uninitialized channels */

	if (channel->toward_ipa) {
		netif_tx_napi_add(&bam->base.dummy_dev, &channel->napi,
				  bam_channel_poll, NAPI_POLL_WEIGHT);
		napi_schedule(&channel->napi);
	} else {
		netif_napi_add(&bam->base.dummy_dev, &channel->napi,
			       bam_channel_poll, NAPI_POLL_WEIGHT);
		napi_schedule(&channel->napi);
	}
	napi_enable(&channel->napi);
}

static void bam_channel_teardown_one(struct bam *bam, u32 channel_id)
{
	struct ipa_channel *channel = &bam->base.channel[channel_id];

	if (!channel->transport)
		return;		/* Ignore uninitialized channels */

	netif_napi_del(&channel->napi);
}

/* Setup function for channels */
static int bam_channel_setup(struct bam *bam)
{
	u32 channel_id = 0;
	int ret;

	mutex_lock(&bam->base.mutex);

	do
		bam_channel_setup_one(bam, channel_id);
	while (++channel_id < BAM_CHANNEL_COUNT_MAX);

	/* Make sure no channels were defined that hardware does not support */
	while (channel_id < BAM_CHANNEL_COUNT_MAX) {
		struct ipa_channel *channel = &bam->base.channel[channel_id++];

		if (!channel->transport)
			continue;	/* Ignore uninitialized channels */

		dev_err(bam->base.dev, "channel %u not supported by hardware\n",
			channel_id - 1);
		channel_id = BAM_CHANNEL_COUNT_MAX;
		goto err_unwind;
	}

	mutex_unlock(&bam->base.mutex);

	return 0;

err_unwind:
	while (channel_id--)
		bam_channel_teardown_one(bam, channel_id);

	mutex_unlock(&bam->base.mutex);

	return ret;
}

/* Inverse of bam_channel_setup() */
static void bam_channel_teardown(struct bam *bam)
{
	u32 channel_id;

	mutex_lock(&bam->base.mutex);

	channel_id = BAM_CHANNEL_COUNT_MAX;
	do
		bam_channel_teardown_one(bam, channel_id);
	while (channel_id--);

	mutex_unlock(&bam->base.mutex);
}

static int bam_setup(struct ipa_transport *transport)
{
	return bam_channel_setup(to_bam(transport));
}

static void bam_teardown(struct ipa_transport *transport)
{
	bam_channel_teardown(to_bam(transport));
}

static u32 bam_channel_tre_max(struct ipa_transport *transport, u32 channel_id)
{
	/*TODO: verify*/
	return BAM_MAX_BURST_SIZE;
}

static u32 bam_channel_trans_tre_max(struct ipa_transport *transport, u32 channel_id)
{
	/*TODO: verify*/
	return BAM_MAX_BURST_SIZE;
}

static int bam_channel_start(struct ipa_transport *transport, u32 channel_id)
{
	/* BAM channels can't be stopped and started */
	return 0;
}

static int bam_channel_stop(struct ipa_transport *transport, u32 channel_id)
{
	/* BAM channels can't be stopped and started */
	return 0;
}

static void bam_channel_reset(struct ipa_transport *transport, u32 channel_id, bool doorbell)
{
	/* No reset for BAM */
}

static int bam_channel_suspend(struct ipa_transport *transport, u32 channel_id, bool stop)
{
	/* No suspend for BAM */
	/* TODO: explore dmaengine api */
	return 0;
}

int bam_channel_resume(struct ipa_transport *transport, u32 channel_id, bool start)
{
	/* No resume for BAM */
	/* TODO: explore dmaengine api */
	return 0;
}

/* Initialize the BAM DMA channels
 * Actual hw init is handled by the BAM_DMA driver
 */
struct ipa_transport *bam_transport_init(struct platform_device *pdev, struct ipa *ipa, u32 count,
					 const struct ipa_gsi_endpoint_data *data)
{
	struct bam *bam;
	struct device *dev = &pdev->dev;
	int ret;

	bam = devm_kzalloc(&pdev->dev, sizeof(*bam), GFP_KERNEL);
	bam->base.ipa = ipa;
	bam->base.dev = dev;
	bam->base.version = ipa->version;

	init_dummy_netdev(&bam->base.dummy_dev);

	ret = bam_channel_init(bam, count, data);
	if (ret)
		return ERR_PTR(ret);

	mutex_init(&bam->base.mutex);

	bam->base.setup = bam_setup;
	bam->base.teardown = bam_teardown;
	bam->base.exit = bam_exit;
	bam->base.channel_tre_max = bam_channel_tre_max;
	bam->base.channel_trans_tre_max = bam_channel_trans_tre_max;
	bam->base.channel_start = bam_channel_start;
	bam->base.channel_stop = bam_channel_stop;
	bam->base.channel_reset = bam_channel_reset;
	bam->base.channel_suspend = bam_channel_suspend;
	bam->base.channel_resume = bam_channel_resume;

	return &bam->base;
}
