// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */

#include <linux/log2.h>

#include "ipa_data.h"
#include "ipa_endpoint.h"
#include "ipa_mem.h"

/* Endpoint configuration for the MSM8953 SoC. */
static const struct ipa_gsi_endpoint_data ipa_gsi_endpoint_data[] = {
	[IPA_ENDPOINT_AP_COMMAND_TX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 3,
		.endpoint_id	= 3,
		.channel_name	= "cmd_tx",
		.toward_ipa	= true,
		.endpoint = {
			.config	= {
				.skip_config	= true,
				.dma_mode	= true,
				.dma_endpoint	= IPA_ENDPOINT_AP_LAN_RX,
			},
		},
	},
	[IPA_ENDPOINT_AP_LAN_RX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 2,
		.endpoint_id	= 2,
		.channel_name	= "ap_lan_rx",
		.endpoint	= {
			.filter_support	= true,
			.config	= {
				.aggregation	= true,
				.status_enable	= true,
				.checksum	= true,
				.rx = {
					.pad_align	= 2,
				},
			},
		},
	},
	[IPA_ENDPOINT_AP_MODEM_TX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 4,
		.endpoint_id	= 4,
		.channel_name	= "ap_modem_tx",
		.toward_ipa	= true,
		.endpoint	= {
			.filter_support	= true,
			.config	= {
				.qmap		= true,
				.checksum	= true,
				.status_enable	= true,
				.tx = {
					.status_endpoint =
						IPA_ENDPOINT_AP_LAN_RX,
				},
			},
		},
	},
	[IPA_ENDPOINT_AP_MODEM_RX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 5,
		.endpoint_id	= 5,
		.channel_name	= "ap_modem_rx",
		.toward_ipa	= false,
		.endpoint	= {
			.filter_support	= true,
			.config = {
				.aggregation	= true,
				.checksum	= true,
				.qmap		= true,
			},
		},
	},
	[IPA_ENDPOINT_MODEM_LAN_TX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 6,
		.endpoint_id	= 6,
		.channel_name	= "modem_lan_tx",
		.toward_ipa	= true,
	},
	[IPA_ENDPOINT_MODEM_COMMAND_TX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 7,
		.endpoint_id	= 7,
		.channel_name	= "modem_cmd_tx",
		.toward_ipa	= true,
	},
	[IPA_ENDPOINT_MODEM_LAN_RX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 8,
		.endpoint_id	= 8,
		.channel_name	= "modem_lan_rx",
		.toward_ipa	= false,
	},
	[IPA_ENDPOINT_MODEM_AP_RX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 9,
		.endpoint_id	= 9,
		.channel_name	= "modem_ap_rx",
		.toward_ipa	= false,
	},
	/* TODO There are other Tx/Rx pairs, like decompression */
};

/* IPA-resident memory region configuration for the MSM8953 SoC. */
static const struct ipa_mem ipa_mem_local_data[] = {
	[IPA_MEM_UC_SHARED] = {
		.offset         = 0,
		.size           = 0x80,
		.canary_count   = 0,
	},
	[IPA_MEM_UC_INFO] = {
		.offset		= 0x0080,
		.size		= 0x0200,
		.canary_count	= 2,
	},
	[IPA_MEM_V4_FILTER] = {
		.offset		= 0x0288,
		.size		= 0x0058,
		.canary_count	= 2,
	},
	[IPA_MEM_V6_FILTER] = {
		.offset		= 0x02e8,
		.size		= 0x0058,
		.canary_count	= 2,
	},
	[IPA_MEM_V4_ROUTE] = {
		.offset		= 0x0348,
		.size		= 0x003c,
		.canary_count	= 1,
	},
	[IPA_MEM_V6_ROUTE] = {
		.offset		= 0x0388,
		.size		= 0x003c,
		.canary_count	= 1,
	},
	[IPA_MEM_MODEM_HEADER] = {
		.offset		= 0x03c8,
		.size		= 0x0140,
		.canary_count	= 2,
	},
	[IPA_MEM_ZIP] = {
		.offset		= 0x0510,
		.size		= 0x0200,
		.canary_count	= 1,
	},
	[IPA_MEM_MODEM] = {
		.offset		= 0x0714,
		.size		= 0x18e8,
		.canary_count	= 1,
	},
};

static struct ipa_mem_data ipa_mem_data = {
	.local_count	= ARRAY_SIZE(ipa_mem_local_data),
	.local		= ipa_mem_local_data,
	.imem_addr	= 0x0, /* no IPA iommu cb */
	.imem_size	= 0x0,
	.smem_id	= 497,
	.smem_size	= 0x00002000,
};

static struct ipa_clock_data ipa_clock_data = {
	.core_clock_rate	= 200 * 1000 * 1000,	/* Hz */
	.interconnect = {
		[IPA_INTERCONNECT_MEMORY] = {
			.peak_rate	= 1200000,	/* 1200 MBps */
			.average_rate	= 100000,	/* 100 MBps */
		},
		/* Average rate is unused for the next two interconnects */
		/*FIXME: check with downstream */
		[IPA_INTERCONNECT_IMEM] = {
			.peak_rate	= 350000,	/* 350 MBps */
			.average_rate	= 0,		/* unused */
		},
		[IPA_INTERCONNECT_CONFIG] = {
			.peak_rate	= 40000,	/* 40 MBps */
			.average_rate	= 0,		/* unused */
		},
	},
};

/* Configuration data for the MSM8953 SoC. */
const struct ipa_data ipa_data_msm8953 = {
	.version	= IPA_VERSION_2_6L,
	.endpoint_count	= ARRAY_SIZE(ipa_gsi_endpoint_data),
	.endpoint_data	= ipa_gsi_endpoint_data,
	.mem_data	= &ipa_mem_data,
	.clock_data	= &ipa_clock_data,
};
