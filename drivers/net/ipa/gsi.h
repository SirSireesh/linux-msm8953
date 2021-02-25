/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2020 Linaro Ltd.
 */
#ifndef _GSI_H_
#define _GSI_H_

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>

#include "ipa_transport.h"
#include "ipa_version.h"
#include "ipa_trans_info.h"

/* Maximum number of channels and event rings supported by the driver */
#define GSI_CHANNEL_COUNT_MAX	17
#define GSI_EVT_RING_COUNT_MAX	13

/* Maximum TLV FIFO size for a channel; 64 here is arbitrary (and high) */
#define GSI_TLV_MAX		64

struct device;
struct scatterlist;
struct platform_device;

struct gsi;
struct ipa_trans;
struct gsi_channel_data;
struct ipa_gsi_endpoint_data;

/* Execution environment IDs */
enum gsi_ee_id {
	GSI_EE_AP				= 0x0,
	GSI_EE_MODEM				= 0x1,
	GSI_EE_UC				= 0x2,
	GSI_EE_TZ				= 0x3,
};

struct gsi_ring {
	void *virt;			/* ring array base address */
	dma_addr_t addr;		/* primarily low 32 bits used */
	u32 count;			/* number of elements in ring */

	/* The ring index value indicates the next "open" entry in the ring.
	 *
	 * A channel ring consists of TRE entries filled by the AP and passed
	 * to the hardware for processing.  For a channel ring, the ring index
	 * identifies the next unused entry to be filled by the AP.
	 *
	 * An event ring consists of event structures filled by the hardware
	 * and passed to the AP.  For event rings, the ring index identifies
	 * the next ring entry that is not known to have been filled by the
	 * hardware.
	 */
	u32 index;
};

/* Transactions use several resources that can be allocated dynamically
 * but taken from a fixed-size pool.  The number of elements required for
 * the pool is limited by the total number of TREs that can be outstanding.
 *
 * If sufficient TREs are available to reserve for a transaction,
 * allocation from these pools is guaranteed to succeed.  Furthermore,
 * these resources are implicitly freed whenever the TREs in the
 * transaction they're associated with are released.
 *
 * The result of a pool allocation of multiple elements is always
 * contiguous.
 */
/* Hardware values signifying the state of a channel */
enum gsi_channel_state {
	GSI_CHANNEL_STATE_NOT_ALLOCATED		= 0x0,
	GSI_CHANNEL_STATE_ALLOCATED		= 0x1,
	GSI_CHANNEL_STATE_STARTED		= 0x2,
	GSI_CHANNEL_STATE_STOPPED		= 0x3,
	GSI_CHANNEL_STATE_STOP_IN_PROC		= 0x4,
	GSI_CHANNEL_STATE_ERROR			= 0xf,
};

/* We only care about channels between IPA and AP */
struct gsi_channel_priv { // This is stored in ipa_channel.priv
	u8 tlv_count;			/* # entries in TLV FIFO */
	u16 tre_count;
	u16 event_count;

	struct gsi_ring tre_ring;
	u32 evt_ring_id;
};

/* Hardware values signifying the state of an event ring */
enum gsi_evt_ring_state {
	GSI_EVT_RING_STATE_NOT_ALLOCATED	= 0x0,
	GSI_EVT_RING_STATE_ALLOCATED		= 0x1,
	GSI_EVT_RING_STATE_ERROR		= 0xf,
};

struct gsi_evt_ring {
	struct ipa_channel *channel;
	struct completion completion;	/* signals event ring state changes */
	enum gsi_evt_ring_state state;
	struct gsi_ring ring;
};

struct gsi {
	struct ipa_transport base;
	void __iomem *virt;
	u32 irq;
	u32 evt_ring_count;
	struct gsi_evt_ring evt_ring[GSI_EVT_RING_COUNT_MAX];
	u32 event_bitmap;		/* allocated event rings */
	u32 modem_channel_bitmap;	/* modem channels to allocate */
	u32 type_enabled_bitmap;	/* GSI IRQ types enabled */
	u32 ieob_enabled_bitmap;	/* IEOB IRQ enabled (event rings) */
	struct completion completion;	/* for global EE commands */
	int result;			/* Negative errno (generic commands) */
};

static inline struct gsi *to_gsi(struct ipa_transport *transport)
{
	return container_of(transport, struct gsi, base);
}

/** TODO
 * gsi_init() - Initialize the GSI subsystem
 * @gsi:	Address of GSI structure embedded in an IPA structure
 * @pdev:	IPA platform device
 * @version:	IPA hardware version (implies GSI version)
 * @count:	Number of entries in the configuration data array
 * @data:	Endpoint and channel configuration data
 *
 * Return:	0 if successful, or a negative error code
 *
 * Early stage initialization of the GSI subsystem, performing tasks
 * that can be done before the GSI hardware is ready to use.
 */
struct ipa_transport* gsi_transport_init(struct platform_device *pdev,
	     struct ipa *ipa, u32 count,
	     const struct ipa_gsi_endpoint_data *data);

#endif /* _GSI_H_ */
