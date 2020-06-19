/*
 * Copyright (c) 2015-2020, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include <dt-bindings/clock/qcom,gcc-msm8953.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

#ifdef CONFIG_DEBUG_FS
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/spinlock.h>
#endif

enum {
	P_XO,
	P_GPLL0,
	P_GPLL2,
	P_GPLL4,
	P_GPLL6,
	P_GPLL0_DIV2,
	P_GPLL0_DIV2_CCI,
	P_GPLL0_DIV2_MM,
	P_GPLL0_DIV2_USB3,
	P_GPLL6_DIV2,
	P_GPLL6_DIV2_GFX,
	P_GPLL6_DIV2_MOCK,
	P_DSI0PLL,
	P_DSI1PLL,
	P_DSI0PLL_BYTE,
	P_DSI1PLL_BYTE,
};

#define DEFMAP(name) static const struct parent_map name##_map[] =
#define DEFNAMES(name) static const char * const name##_names[] =

DEFMAP(xo_g0_g4_g0d) {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL4, 2 },
	{ P_GPLL0_DIV2, 4 },
};

DEFNAMES(xo_g0_g4_g0d) {
	"xo",
	"gpll0",
	"gpll4",
	"gpll0_early_div",
};

DEFMAP(g0_g0d_g2) {
	{ P_GPLL0, 1 },
	{ P_GPLL0_DIV2, 4 },
	{ P_GPLL2, 5 },
};

DEFNAMES(g0_g0d_g2) {
	"gpll0",
	"gpll0_early_div",
	"gpll2",
};

DEFMAP(g0_g0d_g2_g0d) {
	{ P_GPLL0, 1 },
	{ P_GPLL0_DIV2_USB3, 2 },
	{ P_GPLL2, 4 },
	{ P_GPLL0_DIV2_MM, 5 },
};

DEFNAMES(g0_g0d_g2_g0d) {
	"gpll0",
	"gpll0_early_div",
	"gpll2",
	"gpll0_early_div",
};

DEFMAP(xo_g0_g6d_g0d_g4_g0d_g6d) {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL6_DIV2_MOCK, 2 },
	{ P_GPLL0_DIV2_CCI, 3 },
	{ P_GPLL4, 4 },
	{ P_GPLL0_DIV2_MM, 5 },
	{ P_GPLL6_DIV2_GFX, 6 },
};

DEFNAMES(xo_g0_g6d_g0d_g4_g0d_g6d) {
	"xo",
	"gpll0",
	"gpll6_early_div",
	"gpll0_early_div",
	"gpll4",
	"gpll0_early_div",
	"gpll6_early_div",
};

DEFMAP(g0_g6_g2_g0d_g6d) {
	{ P_GPLL0, 1 },
	{ P_GPLL6, 2 },
	{ P_GPLL2, 3 },
	{ P_GPLL0_DIV2, 4 },
	{ P_GPLL6_DIV2, 5 },
};

DEFNAMES(g0_g6_g2_g0d_g6d) {
	"gpll0",
	"gpll6",
	"gpll2",
	"gpll0_early_div",
	"gpll6_early_div"
};

DEFMAP(xo_dsi0pll_dsi1pll) {
	{ P_XO, 0 },
	{ P_DSI0PLL, 1 },
	{ P_DSI1PLL, 3 }
};

DEFNAMES(xo_dsi0pll_dsi1pll) {
	"xo",
	"dsi0pll",
	"dsi1pll"
};

DEFMAP(xo_dsi1pll_dsi0pll) {
	{ P_XO, 0 },
	{ P_DSI1PLL, 1 },
	{ P_DSI0PLL, 3 }
};

DEFNAMES(xo_dsi1pll_dsi0pll) {
	"xo",
	"dsi1pll",
	"dsi0pll"
};

DEFMAP(xo_dsi0pllbyte_dsi1pllbyte) {
	{ P_XO, 0 },
	{ P_DSI0PLL_BYTE, 1 },
	{ P_DSI1PLL_BYTE, 3 }
};

DEFNAMES(xo_dsi0pllbyte_dsi1pllbyte) {
	"xo",
	"dsi0pllbyte",
	"dsi1pllbyte"
};

DEFMAP(xo_dsi1pllbyte_dsi0pllbyte) {
	{ P_XO, 0 },
	{ P_DSI1PLL_BYTE, 1 },
	{ P_DSI0PLL_BYTE, 3 }
};

DEFNAMES(xo_dsi1pllbyte_dsi0pllbyte) {
	"xo",
	"dsi1pllbyte",
	"dsi0pllbyte"
};

static struct clk_fixed_factor xo = {
	.mult = 1,
	.div = 1,
	.hw.init = &(struct clk_init_data){
		.name = "xo",
		.parent_names = (const char *[]){ "xo_board" },
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_alpha_pll gpll0_early = {
	.offset = 0x21000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.enable_reg = 0x45000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data) {
			.num_parents = 1,
			.parent_names = (const char*[]){ "xo" },
			.name = "gpll0_early",
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_fixed_factor gpll0_early_div = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "gpll0_early_div",
		.parent_names = (const char *[]){ "gpll0_early" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_alpha_pll_postdiv gpll0 = {
	.offset = 0x21000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll0",
		.parent_names = (const char *[]){ "gpll0_early" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
};

static struct clk_alpha_pll gpll2_early = {
	.offset = 0x4A000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.enable_reg = 0x45000,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){ "xo" },
			.name ="gpll2_early",
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_alpha_pll_postdiv gpll2 = {
	.offset = 0x4A000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll2",
		.parent_names = (const char *[]){ "gpll2_early" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
};

static struct pll_vco gpll3_p_vco[] = {
	{ 1000000000, 2000000000, 0 },
};

static struct clk_alpha_pll gpll3_early = {
	.offset = 0x22000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.vco_table = gpll3_p_vco,
	.num_vco = ARRAY_SIZE(gpll3_p_vco),
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){ "xo" },
			.name ="gpll3_early",
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_fixed_factor gpll3_early_div = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "gpll3_early_div",
		.parent_names = (const char *[]){ "gpll3_early" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_alpha_pll_postdiv gpll3 = {
	.offset = 0x22000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll3",
		.parent_names = (const char *[]){ "gpll3_early" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
};

static struct clk_alpha_pll gpll4_early = {
	.offset = 0x24000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.enable_reg = 0x45000,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){ "xo" },
			.name ="gpll4_early",
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_alpha_pll_postdiv gpll4 = {
	.offset = 0x24000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll4",
		.parent_names = (const char *[]){ "gpll4_early" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
};

static struct clk_alpha_pll gpll6_early = {
	.offset = 0x37000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.enable_reg = 0x45000,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){ "xo" },
			.name ="gpll6_early",
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_fixed_factor gpll6_early_div = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "gpll6_early_div",
		.parent_names = (const char *[]){ "gpll6_early" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_alpha_pll_postdiv gpll6 = {
	.offset = 0x37000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll6",
		.parent_names = (const char *[]){ "gpll6_early" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
};

static struct freq_tbl ftbl_camss_top_ahb_clk_src[] = {
	F(40000000, P_GPLL0_DIV2, 10, 0, 0),
	F(80000000, P_GPLL0, 10, 0, 0),
	{ }
};

static struct freq_tbl ftbl_csi0_clk_src[] = {
	F(100000000, P_GPLL0_DIV2_MM, 4, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(465000000, P_GPLL2, 2, 0, 0),
	{ }
};

static struct freq_tbl ftbl_apss_ahb_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(25000000, P_GPLL0_DIV2, 16, 0, 0),
	F(50000000, P_GPLL0, 16, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	{ }
};

static struct freq_tbl ftbl_csi1_clk_src[] = {
	F(100000000, P_GPLL0_DIV2, 4, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(465000000, P_GPLL2, 2, 0, 0),
	{ }
};

static struct freq_tbl ftbl_csi2_clk_src[] = {
	F(100000000, P_GPLL0_DIV2, 4, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(465000000, P_GPLL2, 2, 0, 0),
	{ }
};

static struct freq_tbl ftbl_vfe0_clk_src[] = {
	F(50000000, P_GPLL0_DIV2_MM, 8, 0, 0),
	F(100000000, P_GPLL0_DIV2_MM, 4, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	F(160000000, P_GPLL0, 5, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(465000000, P_GPLL2, 2, 0, 0),
	{ }
};

static struct freq_tbl ftbl_gfx3d_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(100000000, P_GPLL0_DIV2_MM, 4, 0, 0),
	F(200000000, P_GPLL0_DIV2_MM, 2, 0, 0),
	F(320000000, P_GPLL0, 2.5, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(460800000, P_GPLL4, 2.5, 0, 0),
	F(533333000, P_GPLL0, 1.5, 0, 0),
	{ }
};

static struct freq_tbl ftbl_vcodec0_clk_src[] = {
	F(114290000, P_GPLL0_DIV2, 3.5, 0, 0),
	F(228570000, P_GPLL0, 3.5, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	F(360000000, P_GPLL6, 3, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(465000000, P_GPLL2, 2, 0, 0),
	{ }
};

static struct freq_tbl ftbl_cpp_clk_src[] = {
	F(100000000, P_GPLL0_DIV2_MM, 4, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(320000000, P_GPLL0, 2.5, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(465000000, P_GPLL2, 2, 0, 0),
	{ }
};

static struct freq_tbl ftbl_jpeg0_clk_src[] = {
	F(66670000, P_GPLL0_DIV2, 6, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	F(320000000, P_GPLL0, 2.5, 0, 0),
	{ }
};

static struct freq_tbl ftbl_mdp_clk_src[] = {
	F(50000000, P_GPLL0_DIV2, 8, 0, 0),
	F(80000000, P_GPLL0_DIV2, 5, 0, 0),
	F(160000000, P_GPLL0_DIV2, 2.5, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(320000000, P_GPLL0, 2.5, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	{ }
};

static struct freq_tbl ftbl_usb30_master_clk_src[] = {
	F(80000000, P_GPLL0_DIV2_USB3, 5, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	{ }
};

static struct freq_tbl ftbl_vfe1_clk_src[] = {
	F(50000000, P_GPLL0_DIV2_MM, 8, 0, 0),
	F(100000000, P_GPLL0_DIV2_MM, 4, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	F(160000000, P_GPLL0, 5, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(465000000, P_GPLL2, 2, 0, 0),
	{ }
};

static struct freq_tbl ftbl_apc0_droop_detector_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(576000000, P_GPLL4, 2, 0, 0),
	{ }
};


static struct freq_tbl ftbl_apc1_droop_detector_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(576000000, P_GPLL4, 2, 0, 0),
	{ }
};


static struct freq_tbl ftbl_blsp_i2c_apps_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(25000000, P_GPLL0_DIV2, 16, 0, 0),
	F(50000000, P_GPLL0, 16, 0, 0),
	{ }
};

static struct freq_tbl ftbl_blsp_spi_apps_clk_src[] = {
	F(960000, P_XO, 10, 1, 2),
	F(4800000, P_XO, 4, 0, 0),
	F(9600000, P_XO, 2, 0, 0),
	F(12500000, P_GPLL0_DIV2, 16, 1, 2),
	F(16000000, P_GPLL0, 10, 1, 5),
	F(19200000, P_XO, 1, 0, 0),
	F(25000000, P_GPLL0, 16, 1, 2),
	F(50000000, P_GPLL0, 16, 0, 0),
	{ }
};

static struct freq_tbl ftbl_blsp_uart_apps_clk_src[] = {
	F(3686400, P_GPLL0_DIV2, 1, 144, 15625),
	F(7372800, P_GPLL0_DIV2, 1, 288, 15625),
	F(14745600, P_GPLL0_DIV2, 1, 576, 15625),
	F(16000000, P_GPLL0_DIV2, 5, 1, 5),
	F(19200000, P_XO, 1, 0, 0),
	F(24000000, P_GPLL0, 1, 3, 100),
	F(25000000, P_GPLL0, 16, 1, 2),
	F(32000000, P_GPLL0, 1, 1, 25),
	F(40000000, P_GPLL0, 1, 1, 20),
	F(46400000, P_GPLL0, 1, 29, 500),
	F(48000000, P_GPLL0, 1, 3, 50),
	F(51200000, P_GPLL0, 1, 8, 125),
	F(56000000, P_GPLL0, 1, 7, 100),
	F(58982400, P_GPLL0, 1, 1152, 15625),
	F(60000000, P_GPLL0, 1, 3, 40),
	F(64000000, P_GPLL0, 1, 2, 25),
	{ }
};

static struct freq_tbl ftbl_cci_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(37500000, P_GPLL0_DIV2_CCI, 1, 3, 32),
	{ }
};

static struct freq_tbl ftbl_csi0p_clk_src[] = {
	F(66670000, P_GPLL0_DIV2_MM, 6, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	{ }
};

static struct freq_tbl ftbl_csi1p_clk_src[] = {
	F(66670000, P_GPLL0_DIV2_MM, 6, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	{ }
};

static struct freq_tbl ftbl_csi2p_clk_src[] = {
	F(66670000, P_GPLL0_DIV2_MM, 6, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	{ }
};

static struct freq_tbl ftbl_camss_gp0_clk_src[] = {
	F(50000000, P_GPLL0_DIV2, 8, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	{ }
};

static struct freq_tbl ftbl_camss_gp1_clk_src[] = {
	F(50000000, P_GPLL0_DIV2, 8, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	{ }
};

static struct freq_tbl ftbl_mclk0_clk_src[] = {
	F(24000000, P_GPLL6_DIV2, 1, 2, 45),
	F(33330000, P_GPLL0_DIV2, 12, 0, 0),
	F(36610000, P_GPLL6, 1, 2, 59),
	F(66667000, P_GPLL0, 12, 0, 0),
	{ }
};

static struct freq_tbl ftbl_mclk1_clk_src[] = {
	F(24000000, P_GPLL6_DIV2, 1, 2, 45),
	F(33330000, P_GPLL0_DIV2, 12, 0, 0),
	F(36610000, P_GPLL6, 1, 2, 59),
	F(66667000, P_GPLL0, 12, 0, 0),
	{ }
};

static struct freq_tbl ftbl_mclk2_clk_src[] = {
	F(24000000, P_GPLL6_DIV2, 1, 2, 45),
	F(33330000, P_GPLL0_DIV2, 12, 0, 0),
	F(36610000, P_GPLL6, 1, 2, 59),
	F(66667000, P_GPLL0, 12, 0, 0),
	{ }
};

static struct freq_tbl ftbl_mclk3_clk_src[] = {
	F(24000000, P_GPLL6_DIV2, 1, 2, 45),
	F(33330000, P_GPLL0_DIV2, 12, 0, 0),
	F(36610000, P_GPLL6, 1, 2, 59),
	F(66667000, P_GPLL0, 12, 0, 0),
	{ }
};

static struct freq_tbl ftbl_csi0phytimer_clk_src[] = {
	F(100000000, P_GPLL0_DIV2, 4, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	{ }
};

static struct freq_tbl ftbl_csi1phytimer_clk_src[] = {
	F(100000000, P_GPLL0_DIV2, 4, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	{ }
};

static struct freq_tbl ftbl_csi2phytimer_clk_src[] = {
	F(100000000, P_GPLL0_DIV2, 4, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	{ }
};

static struct freq_tbl ftbl_crypto_clk_src[] = {
	F(40000000, P_GPLL0_DIV2, 10, 0, 0),
	F(80000000, P_GPLL0, 10, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(160000000, P_GPLL0, 5, 0, 0),
	{ }
};

static struct freq_tbl ftbl_gp1_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct freq_tbl ftbl_gp2_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct freq_tbl ftbl_gp3_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct freq_tbl ftbl_esc0_1_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct freq_tbl ftbl_vsync_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct freq_tbl ftbl_pdm2_clk_src[] = {
	F(32000000, P_GPLL0_DIV2, 12.5, 0, 0),
	F(64000000, P_GPLL0, 12.5, 0, 0),
	{ }
};

static struct freq_tbl ftbl_rbcpr_gfx_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0, 16, 0, 0),
	{ }
};

static struct freq_tbl ftbl_sdcc1_apps_clk_src[] = {
	F(144000, P_XO, 16, 3, 25),
	F(400000, P_XO, 12, 1, 4),
	F(20000000, P_GPLL0_DIV2, 5, 1, 4),
	F(25000000, P_GPLL0_DIV2, 16, 0, 0),
	F(50000000, P_GPLL0, 16, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(177770000, P_GPLL0, 4.5, 0, 0),
	F(192000000, P_GPLL4, 6, 0, 0),
	F(384000000, P_GPLL4, 3, 0, 0),
	{ }
};

static struct freq_tbl ftbl_sdcc1_ice_core_clk_src[] = {
	F(80000000, P_GPLL0_DIV2, 5, 0, 0),
	F(160000000, P_GPLL0, 5, 0, 0),
	F(270000000, P_GPLL6, 4, 0, 0),
	{ }
};

static struct freq_tbl ftbl_sdcc2_apps_clk_src[] = {
	F(144000, P_XO, 16, 3, 25),
	F(400000, P_XO, 12, 1, 4),
	F(20000000, P_GPLL0_DIV2, 5, 1, 4),
	F(25000000, P_GPLL0_DIV2, 16, 0, 0),
	F(50000000, P_GPLL0, 16, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(177770000, P_GPLL0, 4.5, 0, 0),
	F(192000000, P_GPLL4, 6, 0, 0),
	//F(200000000, P_GPLL0, 4, 0, 0),
	{ }
};

static struct freq_tbl ftbl_usb30_mock_utmi_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(60000000, P_GPLL6_DIV2_MOCK, 9, 1, 1),
	{ }
};

static struct freq_tbl ftbl_usb3_aux_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

#define RCG(name, ops, addr, parents, mnd, hid, ftbl, flags)				\
	static struct clk_rcg2 name = {							\
		.cmd_rcgr = addr,							\
		.hid_width = hid,							\
		.mnd_width = mnd,							\
		.freq_tbl = ftbl,							\
		.parent_map = parents##_map,						\
		.clkr.hw.init = CLK_HW_INIT_PARENTS(#name, parents##_names, &ops, flags) \
	}

RCG(camss_top_ahb_clk_src,       clk_rcg2_ops, 0x5A000, g0_g0d_g2,                16,  5, ftbl_camss_top_ahb_clk_src, 0);
RCG(csi0_clk_src,                clk_rcg2_ops, 0x4E020, g0_g0d_g2_g0d,            0,   5, ftbl_csi0_clk_src, 0);
RCG(apss_ahb_clk_src,            clk_rcg2_ops, 0x46000, xo_g0_g4_g0d,             0,   5, ftbl_apss_ahb_clk_src, 0);
RCG(csi1_clk_src,                clk_rcg2_ops, 0x4F020, g0_g0d_g2,                0,   5, ftbl_csi1_clk_src, 0);
RCG(csi2_clk_src,                clk_rcg2_ops, 0x3C020, g0_g0d_g2,                0,   5, ftbl_csi2_clk_src, 0);
RCG(vfe0_clk_src,                clk_rcg2_ops, 0x58000, g0_g0d_g2_g0d,            0,   5, ftbl_vfe0_clk_src, 0);
RCG(gfx3d_clk_src,               clk_rcg2_ops, 0x59000, xo_g0_g6d_g0d_g4_g0d_g6d, 0,   5, ftbl_gfx3d_clk_src, 0);
RCG(vcodec0_clk_src,             clk_rcg2_ops, 0x4C000, g0_g6_g2_g0d_g6d,         16,  5, ftbl_vcodec0_clk_src, 0);
RCG(cpp_clk_src,                 clk_rcg2_ops, 0x58018, g0_g0d_g2_g0d,            0,   5, ftbl_cpp_clk_src, 0);
RCG(jpeg0_clk_src,               clk_rcg2_ops, 0x57000, g0_g0d_g2,                0,   5, ftbl_jpeg0_clk_src, 0);
RCG(mdp_clk_src,                 clk_rcg2_ops, 0x4D014, g0_g0d_g2,                0,   5, ftbl_mdp_clk_src, 0);
RCG(usb30_master_clk_src,        clk_rcg2_ops, 0x3F00C, g0_g0d_g2_g0d,            16,  5, ftbl_usb30_master_clk_src, 0);
RCG(vfe1_clk_src,                clk_rcg2_ops, 0x58054, g0_g0d_g2_g0d,            0,   5, ftbl_vfe1_clk_src, 0);
RCG(apc0_droop_detector_clk_src, clk_rcg2_ops, 0x78008, xo_g0_g4_g0d,             0,   5, ftbl_apc0_droop_detector_clk_src, 0);
RCG(apc1_droop_detector_clk_src, clk_rcg2_ops, 0x79008, xo_g0_g4_g0d,             0,   5, ftbl_apc1_droop_detector_clk_src, 0);
RCG(blsp1_qup1_i2c_apps_clk_src, clk_rcg2_ops, 0x0200C, xo_g0_g4_g0d,             0,   5, ftbl_blsp_i2c_apps_clk_src, 0);
RCG(blsp1_qup1_spi_apps_clk_src, clk_rcg2_ops, 0x02024, xo_g0_g4_g0d,             16,  5, ftbl_blsp_spi_apps_clk_src, 0);
RCG(blsp1_qup2_i2c_apps_clk_src, clk_rcg2_ops, 0x03000, xo_g0_g4_g0d,             0,   5, ftbl_blsp_i2c_apps_clk_src, 0);
RCG(blsp1_qup2_spi_apps_clk_src, clk_rcg2_ops, 0x03014, xo_g0_g4_g0d,             16,  5, ftbl_blsp_spi_apps_clk_src, 0);
RCG(blsp1_qup3_i2c_apps_clk_src, clk_rcg2_ops, 0x04000, xo_g0_g4_g0d,             0,   5, ftbl_blsp_i2c_apps_clk_src, 0);
RCG(blsp1_qup3_spi_apps_clk_src, clk_rcg2_ops, 0x04024, xo_g0_g4_g0d,             16,  5, ftbl_blsp_spi_apps_clk_src, 0);
RCG(blsp1_qup4_i2c_apps_clk_src, clk_rcg2_ops, 0x05000, xo_g0_g4_g0d,             0,   5, ftbl_blsp_i2c_apps_clk_src, 0);
RCG(blsp1_qup4_spi_apps_clk_src, clk_rcg2_ops, 0x05024, xo_g0_g4_g0d,             16,  5, ftbl_blsp_spi_apps_clk_src, 0);
RCG(blsp1_uart1_apps_clk_src,    clk_rcg2_ops, 0x02044, xo_g0_g4_g0d,             16,  5, ftbl_blsp_uart_apps_clk_src, 0);
RCG(blsp1_uart2_apps_clk_src,    clk_rcg2_ops, 0x03034, xo_g0_g4_g0d,             16,  5, ftbl_blsp_uart_apps_clk_src, 0);
RCG(blsp2_qup1_i2c_apps_clk_src, clk_rcg2_ops, 0x0C00C, xo_g0_g4_g0d,             0,   5, ftbl_blsp_i2c_apps_clk_src, 0);
RCG(blsp2_qup1_spi_apps_clk_src, clk_rcg2_ops, 0x0C024, xo_g0_g4_g0d,             16,  5, ftbl_blsp_spi_apps_clk_src, 0);
RCG(blsp2_qup2_i2c_apps_clk_src, clk_rcg2_ops, 0x0D000, xo_g0_g4_g0d,             0,   5, ftbl_blsp_i2c_apps_clk_src, 0);
RCG(blsp2_qup2_spi_apps_clk_src, clk_rcg2_ops, 0x0D014, xo_g0_g4_g0d,             16,  5, ftbl_blsp_spi_apps_clk_src, 0);
RCG(blsp2_qup3_i2c_apps_clk_src, clk_rcg2_ops, 0x0F000, xo_g0_g4_g0d,             0,   5, ftbl_blsp_i2c_apps_clk_src, 0);
RCG(blsp2_qup3_spi_apps_clk_src, clk_rcg2_ops, 0x0F024, xo_g0_g4_g0d,             16,  5, ftbl_blsp_spi_apps_clk_src, 0);
RCG(blsp2_qup4_i2c_apps_clk_src, clk_rcg2_ops, 0x18000, xo_g0_g4_g0d,             0,   5, ftbl_blsp_i2c_apps_clk_src, 0);
RCG(blsp2_qup4_spi_apps_clk_src, clk_rcg2_ops, 0x18024, xo_g0_g4_g0d,             16,  5, ftbl_blsp_spi_apps_clk_src, 0);
RCG(blsp2_uart1_apps_clk_src,    clk_rcg2_ops, 0x0C044, xo_g0_g4_g0d,             16,  5, ftbl_blsp_uart_apps_clk_src, 0);
RCG(blsp2_uart2_apps_clk_src,    clk_rcg2_ops, 0x0D034, xo_g0_g4_g0d,             16,  5, ftbl_blsp_uart_apps_clk_src, 0);
RCG(cci_clk_src,                 clk_rcg2_ops, 0x51000, xo_g0_g6d_g0d_g4_g0d_g6d, 16,  5, ftbl_cci_clk_src, 0);
RCG(csi0p_clk_src,               clk_rcg2_ops, 0x58084, g0_g0d_g2_g0d,            0,   5, ftbl_csi0p_clk_src, 0);
RCG(csi1p_clk_src,               clk_rcg2_ops, 0x58094, g0_g0d_g2_g0d,            0,   5, ftbl_csi1p_clk_src, 0);
RCG(csi2p_clk_src,               clk_rcg2_ops, 0x580A4, g0_g0d_g2_g0d,            0,   5, ftbl_csi2p_clk_src, 0);
RCG(camss_gp0_clk_src,           clk_rcg2_ops, 0x54000, g0_g0d_g2,                16,  5, ftbl_camss_gp0_clk_src, 0);
RCG(camss_gp1_clk_src,           clk_rcg2_ops, 0x55000, g0_g0d_g2,                16,  5, ftbl_camss_gp1_clk_src, 0);
RCG(mclk0_clk_src,               clk_rcg2_ops, 0x52000, g0_g6_g2_g0d_g6d,         16,  5, ftbl_mclk0_clk_src, 0);
RCG(mclk1_clk_src,               clk_rcg2_ops, 0x53000, g0_g6_g2_g0d_g6d,         16,  5, ftbl_mclk1_clk_src, 0);
RCG(mclk2_clk_src,               clk_rcg2_ops, 0x5C000, g0_g6_g2_g0d_g6d,         16,  5, ftbl_mclk2_clk_src, 0);
RCG(mclk3_clk_src,               clk_rcg2_ops, 0x5E000, g0_g6_g2_g0d_g6d,         16,  5, ftbl_mclk3_clk_src, 0);
RCG(csi0phytimer_clk_src,        clk_rcg2_ops, 0x4E000, g0_g0d_g2,                0,   5, ftbl_csi0phytimer_clk_src, 0);
RCG(csi1phytimer_clk_src,        clk_rcg2_ops, 0x4F000, xo_g0_g4_g0d,             0,   5, ftbl_csi1phytimer_clk_src, 0);
RCG(csi2phytimer_clk_src,        clk_rcg2_ops, 0x4F05C, xo_g0_g4_g0d,             0,   5, ftbl_csi2phytimer_clk_src, 0);
RCG(crypto_clk_src,              clk_rcg2_ops, 0x16004, xo_g0_g4_g0d,             0,   5, ftbl_crypto_clk_src, 0);
RCG(gp1_clk_src,                 clk_rcg2_ops, 0x08004, xo_g0_g4_g0d,             16,  5, ftbl_gp1_clk_src, 0);
RCG(gp2_clk_src,                 clk_rcg2_ops, 0x09004, xo_g0_g4_g0d,             16,  5, ftbl_gp2_clk_src, 0);
RCG(gp3_clk_src,                 clk_rcg2_ops, 0x0A004, xo_g0_g4_g0d,             16,  5, ftbl_gp3_clk_src, 0);
RCG(esc0_clk_src,                clk_rcg2_ops, 0x4D05C, xo_g0_g4_g0d,             0,   5, ftbl_esc0_1_clk_src, 0);
RCG(esc1_clk_src,                clk_rcg2_ops, 0x4D0A8, xo_g0_g4_g0d,             0,   5, ftbl_esc0_1_clk_src, 0);
RCG(vsync_clk_src,               clk_rcg2_ops, 0x4D02C, xo_g0_g4_g0d,             0,   5, ftbl_vsync_clk_src, 0);
RCG(pdm2_clk_src,                clk_rcg2_ops, 0x44010, g0_g0d_g2,                0,   5, ftbl_pdm2_clk_src, 0);
RCG(rbcpr_gfx_clk_src,           clk_rcg2_ops, 0x3A00C, xo_g0_g4_g0d,             0,   5, ftbl_rbcpr_gfx_clk_src, 0);
RCG(sdcc1_ice_core_clk_src,      clk_rcg2_ops, 0x5D000, g0_g6_g2_g0d_g6d,         16,  5, ftbl_sdcc1_ice_core_clk_src, 0);
RCG(usb30_mock_utmi_clk_src,     clk_rcg2_ops, 0x3F020, xo_g0_g6d_g0d_g4_g0d_g6d, 16,  5, ftbl_usb30_mock_utmi_clk_src, 0);
RCG(usb3_aux_clk_src,            clk_rcg2_ops, 0x3F05C, xo_g0_g4_g0d,             16,  5, ftbl_usb3_aux_clk_src, 0);
RCG(sdcc1_apps_clk_src,          clk_rcg2_floor_ops, 0x42004, xo_g0_g4_g0d,       16,  5, ftbl_sdcc1_apps_clk_src, 0);
RCG(sdcc2_apps_clk_src,          clk_rcg2_floor_ops, 0x43004, xo_g0_g4_g0d,       16,  5, ftbl_sdcc2_apps_clk_src, 0);
RCG(pclk0_clk_src,               clk_pixel_ops, 0x4D000, xo_dsi0pll_dsi1pll, 8, 5, NULL, CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED);
RCG(pclk1_clk_src,               clk_pixel_ops, 0x4D0B8, xo_dsi1pll_dsi0pll, 8, 5, NULL, CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED);
RCG(byte0_clk_src,               clk_byte2_ops, 0x4D044, xo_dsi0pllbyte_dsi1pllbyte, 0, 5, NULL, CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED);
RCG(byte1_clk_src,               clk_byte2_ops, 0x4D0B0, xo_dsi1pllbyte_dsi0pllbyte, 0, 5, NULL, CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED);

#define BRANCH(name, en_reg, hlt_reg, parent, ops, flags, en_mask, hlt_check)	\
	static struct clk_branch name = {					\
		.halt_reg = hlt_reg,						\
		.halt_check = hlt_check,					\
		.clkr = {							\
			.enable_reg = en_reg,					\
			.enable_mask = en_mask,					\
			.hw.init = CLK_HW_INIT(#name, #parent, &ops, flags)	\
		}								\
	}

BRANCH(gcc_blsp1_uart1_apps_clk,     0x0203C, 0x0203C, blsp1_uart1_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_blsp1_uart2_apps_clk,     0x0302C, 0x0302C, blsp2_uart2_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_blsp2_uart1_apps_clk,     0x0C03C, 0x0C03C, blsp1_uart1_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_blsp2_uart2_apps_clk,     0x0D02C, 0x0D02C, blsp2_uart2_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_blsp1_qup1_i2c_apps_clk,  0x02008, 0x02008, blsp1_qup1_i2c_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_blsp1_qup2_i2c_apps_clk,  0x03010, 0x03010, blsp1_qup2_i2c_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_blsp1_qup3_i2c_apps_clk,  0x04020, 0x04020, blsp1_qup3_i2c_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_blsp1_qup4_i2c_apps_clk,  0x05020, 0x05020, blsp1_qup4_i2c_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_blsp2_qup1_i2c_apps_clk,  0x0C008, 0x0C008, blsp2_qup1_i2c_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_blsp2_qup2_i2c_apps_clk,  0x0D010, 0x0D010, blsp2_qup2_i2c_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_blsp2_qup3_i2c_apps_clk,  0x0F020, 0x0F020, blsp2_qup3_i2c_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_blsp2_qup4_i2c_apps_clk,  0x18020, 0x18020, blsp2_qup4_i2c_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_blsp1_qup1_spi_apps_clk,  0x02004, 0x02004, blsp1_qup1_spi_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_blsp1_qup2_spi_apps_clk,  0x0300C, 0x0300C, blsp1_qup2_spi_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_blsp1_qup3_spi_apps_clk,  0x0401C, 0x0401C, blsp1_qup3_spi_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_blsp1_qup4_spi_apps_clk,  0x0501C, 0x0501C, blsp1_qup4_spi_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_blsp2_qup1_spi_apps_clk,  0x0C004, 0x0C004, blsp2_qup1_spi_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_blsp2_qup2_spi_apps_clk,  0x0D00C, 0x0D00C, blsp2_qup2_spi_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_blsp2_qup3_spi_apps_clk,  0x0F01C, 0x0F01C, blsp2_qup3_spi_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_blsp2_qup4_spi_apps_clk,  0x1801C, 0x1801C, blsp2_qup4_spi_apps_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_bimc_gpu_clk,             0x59030, 0x59030, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_cci_ahb_clk,        0x5101C, 0x5101C, camss_top_ahb_clk_src,    clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_cci_clk,            0x51018, 0x51018, cci_clk_src,              clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_cpp_ahb_clk,        0x58040, 0x58040, camss_top_ahb_clk_src,    clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_cpp_axi_clk,        0x58064, 0x58064, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_cpp_clk,            0x5803C, 0x5803C, cpp_clk_src,              clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi0_ahb_clk,       0x4E040, 0x4E040, camss_top_ahb_clk_src,    clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi0_clk,           0x4E03C, 0x4E03C, csi0_clk_src,             clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi0_csiphy_3p_clk, 0x58090, 0x58090, csi0p_clk_src,            clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi0phy_clk,        0x4E048, 0x4E048, csi0_clk_src,             clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi0pix_clk,        0x4E058, 0x4E058, csi0_clk_src,             clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi0rdi_clk,        0x4E050, 0x4E050, csi0_clk_src,             clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi1_ahb_clk,       0x4F040, 0x4F040, camss_top_ahb_clk_src,    clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi1_clk,           0x4F03C, 0x4F03C, csi1_clk_src,             clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi1_csiphy_3p_clk, 0x580A0, 0x580A0, csi1p_clk_src,            clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi1phy_clk,        0x4F048, 0x4F048, csi1_clk_src,             clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi1pix_clk,        0x4F058, 0x4F058, csi1_clk_src,             clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi1rdi_clk,        0x4F050, 0x4F050, csi1_clk_src,             clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi2_ahb_clk,       0x3C040, 0x3C040, camss_top_ahb_clk_src,    clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi2_clk,           0x3C03C, 0x3C03C, csi2_clk_src,             clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi2_csiphy_3p_clk, 0x580B0, 0x580B0, csi2p_clk_src,            clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi2phy_clk,        0x3C048, 0x3C048, csi2_clk_src,             clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi2pix_clk,        0x3C058, 0x3C058, csi2_clk_src,             clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi2rdi_clk,        0x3C050, 0x3C050, csi2_clk_src,             clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi_vfe0_clk,       0x58050, 0x58050, vfe0_clk_src,             clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi_vfe1_clk,       0x58074, 0x58074, vfe1_clk_src,             clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_gp0_clk,            0x54018, 0x54018, camss_gp0_clk_src,        clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_gp1_clk,            0x55018, 0x55018, camss_gp1_clk_src,        clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_ispif_ahb_clk,      0x50004, 0x50004, camss_top_ahb_clk_src,    clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_jpeg0_clk,          0x57020, 0x57020, jpeg0_clk_src,            clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_jpeg_ahb_clk,       0x57024, 0x57024, camss_top_ahb_clk_src,    clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_jpeg_axi_clk,       0x57028, 0x57028, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_mclk0_clk,          0x52018, 0x52018, mclk0_clk_src,            clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_mclk1_clk,          0x53018, 0x53018, mclk1_clk_src,            clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_mclk2_clk,          0x5C018, 0x5C018, mclk2_clk_src,            clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_mclk3_clk,          0x5E018, 0x5E018, mclk3_clk_src,            clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_micro_ahb_clk,      0x5600C, 0x5600C, camss_top_ahb_clk_src,    clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi0phytimer_clk,   0x4E01C, 0x4E01C, csi0phytimer_clk_src,     clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi1phytimer_clk,   0x4F01C, 0x4F01C, csi1phytimer_clk_src,     clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_csi2phytimer_clk,   0x4F068, 0x4F068, csi2phytimer_clk_src,     clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_ahb_clk,            0x56004, 0x56004, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_top_ahb_clk,        0x5A014, 0x5A014, camss_top_ahb_clk_src,    clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_vfe0_clk,           0x58038, 0x58038, vfe0_clk_src,             clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_vfe_ahb_clk,        0x58044, 0x58044, camss_top_ahb_clk_src,    clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_vfe_axi_clk,        0x58048, 0x58048, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_vfe1_ahb_clk,       0x58060, 0x58060, camss_top_ahb_clk_src,    clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_vfe1_axi_clk,       0x58068, 0x58068, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_camss_vfe1_clk,           0x5805C, 0x5805C, vfe1_clk_src,             clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_dcc_clk,                  0x77004, 0x77004, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_gp1_clk,                  0x08000, 0x08000, gp1_clk_src,              clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_gp2_clk,                  0x09000, 0x09000, gp2_clk_src,              clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_gp3_clk,                  0x0A000, 0x0A000, gp3_clk_src,              clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_mdss_ahb_clk,             0x4D07C, 0x4D07C, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_mdss_axi_clk,             0x4D080, 0x4D080, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_mdss_esc0_clk,            0x4D098, 0x4D098, esc0_clk_src,             clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_mdss_esc1_clk,            0x4D09C, 0x4D09C, esc1_clk_src,             clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_mdss_mdp_clk,             0x4D088, 0x4D088, mdp_clk_src,              clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_mdss_vsync_clk,           0x4D090, 0x4D090, vsync_clk_src,            clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_mss_cfg_ahb_clk,          0x49000, 0x49000, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_mss_q6_bimc_axi_clk,      0x49004, 0x49004, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_bimc_gfx_clk,             0x59034, 0x59034, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_oxili_ahb_clk,            0x59028, 0x59028, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_oxili_aon_clk,            0x59044, 0x59044, gfx3d_clk_src,            clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_oxili_gfx3d_clk,          0x59020, 0x59020, gfx3d_clk_src,            clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_oxili_timer_clk,          0x59040, 0x59040, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_pcnoc_usb3_axi_clk,       0x3F038, 0x3F038, usb30_master_clk_src,     clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_pdm2_clk,                 0x4400C, 0x4400C, pdm2_clk_src,             clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_pdm_ahb_clk,              0x44004, 0x44004, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_rbcpr_gfx_clk,            0x3A004, 0x3A004, rbcpr_gfx_clk_src,        clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_sdcc1_ahb_clk,            0x4201C, 0x4201C, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_sdcc1_apps_clk,           0x42018, 0x42018, sdcc1_apps_clk_src,       clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_sdcc1_ice_core_clk,       0x5D014, 0x5D014, sdcc1_ice_core_clk_src,   clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_sdcc2_ahb_clk,            0x4301C, 0x4301C, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_sdcc2_apps_clk,           0x43018, 0x43018, sdcc2_apps_clk_src,       clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_usb30_master_clk,         0x3F000, 0x3F000, usb30_master_clk_src,     clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_usb30_mock_utmi_clk,      0x3F008, 0x3F008, usb30_mock_utmi_clk_src,  clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_usb30_sleep_clk,          0x3F004, 0x3F004, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_usb3_aux_clk,             0x3F044, 0x3F044, usb3_aux_clk_src,         clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_usb_phy_cfg_ahb_clk,      0x3F080, 0x3F080, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_VOTED);
BRANCH(gcc_venus0_ahb_clk,           0x4C020, 0x4C020, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_venus0_axi_clk,           0x4C024, 0x4C024, xo,                       clk_branch2_ops, 0,                   BIT(0), BRANCH_HALT);
BRANCH(gcc_venus0_core0_vcodec0_clk, 0x4C02C, 0x4C02C, vcodec0_clk_src,          clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_venus0_vcodec0_clk,       0x4C01C, 0x4C01C, vcodec0_clk_src,          clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_qusb_ref_clk,             0x41030, 0,       bb_clk1,                  clk_branch_ops,  0,                   BIT(0), BRANCH_HALT_SKIP);
BRANCH(gcc_usb_ss_ref_clk,           0x3F07C, 0,       bb_clk1,                  clk_branch_ops,  0,                   BIT(0), BRANCH_HALT_SKIP);
BRANCH(gcc_usb3_pipe_clk,            0x3F040, 0,       gcc_usb3_pipe_clk_src,    clk_branch_ops,  CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT_DELAY);
BRANCH(gcc_apss_ahb_clk,             0x45004, 0x4601C, apss_ahb_clk_src,         clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(14), BRANCH_HALT_VOTED);
BRANCH(gcc_apss_axi_clk,             0x45004, 0x46020, xo,                       clk_branch2_ops, 0,                   BIT(13), BRANCH_HALT_VOTED);
BRANCH(gcc_blsp1_ahb_clk,            0x45004, 0x01008, xo,                       clk_branch2_ops, 0,                   BIT(10), BRANCH_HALT_VOTED);
BRANCH(gcc_blsp2_ahb_clk,            0x45004, 0x0B008, xo,                       clk_branch2_ops, 0,                   BIT(20), BRANCH_HALT_VOTED);
BRANCH(gcc_boot_rom_ahb_clk,         0x45004, 0x1300C, xo,                       clk_branch2_ops, 0,                   BIT(7),  BRANCH_HALT_VOTED);
BRANCH(gcc_crypto_ahb_clk,           0x45004, 0x16024, xo,                       clk_branch2_ops, 0,                   BIT(0),  BRANCH_HALT_VOTED);
BRANCH(gcc_crypto_axi_clk,           0x45004, 0x16020, xo,                       clk_branch2_ops, 0,                   BIT(1),  BRANCH_HALT_VOTED);
BRANCH(gcc_crypto_clk,               0x45004, 0x1601C, crypto_clk_src,           clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(2),  BRANCH_HALT_VOTED);
BRANCH(gcc_qdss_dap_clk,             0x45004, 0x29084, xo,                       clk_branch2_ops, 0,                   BIT(11), BRANCH_HALT_VOTED);
BRANCH(gcc_prng_ahb_clk,             0x45004, 0x13004, xo,                       clk_branch2_ops, 0,                   BIT(8),  BRANCH_HALT_VOTED);
BRANCH(gcc_apss_tcu_async_clk,       0x4500C, 0x12018, xo,                       clk_branch2_ops, 0,                   BIT(1),  BRANCH_HALT_VOTED);
BRANCH(gcc_cpp_tbu_clk,              0x4500C, 0x12040, xo,                       clk_branch2_ops, 0,                   BIT(14), BRANCH_HALT_VOTED);
BRANCH(gcc_jpeg_tbu_clk,             0x4500C, 0x12034, xo,                       clk_branch2_ops, 0,                   BIT(10), BRANCH_HALT_VOTED);
BRANCH(gcc_mdp_tbu_clk,              0x4500C, 0x1201C, xo,                       clk_branch2_ops, 0,                   BIT(4),  BRANCH_HALT_VOTED);
BRANCH(gcc_smmu_cfg_clk,             0x4500C, 0x12038, xo,                       clk_branch2_ops, 0,                   BIT(12), BRANCH_HALT_VOTED);
BRANCH(gcc_venus_tbu_clk,            0x4500C, 0x12014, xo,                       clk_branch2_ops, 0,                   BIT(5),  BRANCH_HALT_VOTED);
BRANCH(gcc_vfe1_tbu_clk,             0x4500C, 0x12090, xo,                       clk_branch2_ops, 0,                   BIT(17), BRANCH_HALT_VOTED);
BRANCH(gcc_vfe_tbu_clk,              0x4500C, 0x1203C, xo,                       clk_branch2_ops, 0,                   BIT(9),  BRANCH_HALT_VOTED);
BRANCH(gcc_apc0_droop_detector_gpll0_clk,0x78004, 0x78004, apc0_droop_detector_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_apc1_droop_detector_gpll0_clk,0x79004, 0x79004, apc1_droop_detector_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT, BIT(0), BRANCH_HALT);
BRANCH(gcc_mdss_byte0_clk, 0x4D094, 0x4D094, byte0_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, BIT(0), BRANCH_HALT);
BRANCH(gcc_mdss_byte1_clk, 0x4D0A0, 0x4D0A0, byte1_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, BIT(0), BRANCH_HALT);
BRANCH(gcc_mdss_pclk0_clk, 0x4D084, 0x4D084, pclk0_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, BIT(0), BRANCH_HALT);
BRANCH(gcc_mdss_pclk1_clk, 0x4D0A4, 0x4D0A4, pclk1_clk_src, clk_branch2_ops, CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, BIT(0), BRANCH_HALT);

static struct gdsc usb30_gdsc = {
	.gdscr = 0x3f078,
	.pd = {
		.name = "usb30_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc venus_gdsc = {
	.gdscr = 0x4c018,
	.pd = {
		.name = "venus",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc mdss_gdsc = {
	.gdscr = 0x4d078,
	.pd = {
		.name = "mdss",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc jpeg_gdsc = {
	.gdscr = 0x5701c,
	.pd = {
		.name = "jpeg",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc vfe_gdsc = {
	.gdscr = 0x58034,
	.pd = {
		.name = "vfe",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc oxili_gx_gdsc = {
	.gdscr = 0x5901c,
	.pd = {
		.name = "oxili_gx",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc oxili_cx_gdsc = {
	.gdscr = 0x5904c,
	.pd = {
		.name = "oxili_cx",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct clk_hw *gcc_msm8953_hws[] = {
	&xo.hw,
	&gpll0_early_div.hw,
	&gpll3_early_div.hw,
	&gpll6_early_div.hw,
};

static struct clk_regmap *gcc_msm8953_clocks[] = {
	[GPLL0] = &gpll0.clkr,
	[GPLL0_EARLY] = &gpll0_early.clkr,
	[GPLL2] = &gpll2.clkr,
	[GPLL2_EARLY] = &gpll2_early.clkr,
	[GPLL3] = &gpll3.clkr,
	[GPLL3_EARLY] = &gpll3_early.clkr,
	[GPLL4] = &gpll4.clkr,
	[GPLL4_EARLY] = &gpll4_early.clkr,
	[GPLL6] = &gpll6.clkr,
	[GPLL6_EARLY] = &gpll6_early.clkr,
	[GCC_APSS_AHB_CLK] = &gcc_apss_ahb_clk.clkr,
	[GCC_APSS_AXI_CLK] = &gcc_apss_axi_clk.clkr,
	[GCC_BLSP1_AHB_CLK] = &gcc_blsp1_ahb_clk.clkr,
	[GCC_BLSP2_AHB_CLK] = &gcc_blsp2_ahb_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CRYPTO_AHB_CLK] = &gcc_crypto_ahb_clk.clkr,
	[GCC_CRYPTO_AXI_CLK] = &gcc_crypto_axi_clk.clkr,
	[GCC_CRYPTO_CLK] = &gcc_crypto_clk.clkr,
	[GCC_PRNG_AHB_CLK] = &gcc_prng_ahb_clk.clkr,
	[GCC_QDSS_DAP_CLK] = &gcc_qdss_dap_clk.clkr,
	[GCC_APSS_TCU_ASYNC_CLK] = &gcc_apss_tcu_async_clk.clkr,
	[GCC_CPP_TBU_CLK] = &gcc_cpp_tbu_clk.clkr,
	[GCC_JPEG_TBU_CLK] = &gcc_jpeg_tbu_clk.clkr,
	[GCC_MDP_TBU_CLK] = &gcc_mdp_tbu_clk.clkr,
	[GCC_SMMU_CFG_CLK] = &gcc_smmu_cfg_clk.clkr,
	[GCC_VENUS_TBU_CLK] = &gcc_venus_tbu_clk.clkr,
	[GCC_VFE1_TBU_CLK] = &gcc_vfe1_tbu_clk.clkr,
	[GCC_VFE_TBU_CLK] = &gcc_vfe_tbu_clk.clkr,
	[CAMSS_TOP_AHB_CLK_SRC] = &camss_top_ahb_clk_src.clkr,
	[CSI0_CLK_SRC] = &csi0_clk_src.clkr,
	[APSS_AHB_CLK_SRC] = &apss_ahb_clk_src.clkr,
	[CSI1_CLK_SRC] = &csi1_clk_src.clkr,
	[CSI2_CLK_SRC] = &csi2_clk_src.clkr,
	[VFE0_CLK_SRC] = &vfe0_clk_src.clkr,
	[VCODEC0_CLK_SRC] = &vcodec0_clk_src.clkr,
	[CPP_CLK_SRC] = &cpp_clk_src.clkr,
	[JPEG0_CLK_SRC] = &jpeg0_clk_src.clkr,
	[USB30_MASTER_CLK_SRC] = &usb30_master_clk_src.clkr,
	[VFE1_CLK_SRC] = &vfe1_clk_src.clkr,
	[APC0_DROOP_DETECTOR_CLK_SRC] = &apc0_droop_detector_clk_src.clkr,
	[APC1_DROOP_DETECTOR_CLK_SRC] = &apc1_droop_detector_clk_src.clkr,
	[BLSP1_QUP1_I2C_APPS_CLK_SRC] = &blsp1_qup1_i2c_apps_clk_src.clkr,
	[BLSP1_QUP1_SPI_APPS_CLK_SRC] = &blsp1_qup1_spi_apps_clk_src.clkr,
	[BLSP1_QUP2_I2C_APPS_CLK_SRC] = &blsp1_qup2_i2c_apps_clk_src.clkr,
	[BLSP1_QUP2_SPI_APPS_CLK_SRC] = &blsp1_qup2_spi_apps_clk_src.clkr,
	[BLSP1_QUP3_I2C_APPS_CLK_SRC] = &blsp1_qup3_i2c_apps_clk_src.clkr,
	[BLSP1_QUP3_SPI_APPS_CLK_SRC] = &blsp1_qup3_spi_apps_clk_src.clkr,
	[BLSP1_QUP4_I2C_APPS_CLK_SRC] = &blsp1_qup4_i2c_apps_clk_src.clkr,
	[BLSP1_QUP4_SPI_APPS_CLK_SRC] = &blsp1_qup4_spi_apps_clk_src.clkr,
	[BLSP1_UART1_APPS_CLK_SRC] = &blsp1_uart1_apps_clk_src.clkr,
	[BLSP1_UART2_APPS_CLK_SRC] = &blsp1_uart2_apps_clk_src.clkr,
	[BLSP2_QUP1_I2C_APPS_CLK_SRC] = &blsp2_qup1_i2c_apps_clk_src.clkr,
	[BLSP2_QUP1_SPI_APPS_CLK_SRC] = &blsp2_qup1_spi_apps_clk_src.clkr,
	[BLSP2_QUP2_I2C_APPS_CLK_SRC] = &blsp2_qup2_i2c_apps_clk_src.clkr,
	[BLSP2_QUP2_SPI_APPS_CLK_SRC] = &blsp2_qup2_spi_apps_clk_src.clkr,
	[BLSP2_QUP3_I2C_APPS_CLK_SRC] = &blsp2_qup3_i2c_apps_clk_src.clkr,
	[BLSP2_QUP3_SPI_APPS_CLK_SRC] = &blsp2_qup3_spi_apps_clk_src.clkr,
	[BLSP2_QUP4_I2C_APPS_CLK_SRC] = &blsp2_qup4_i2c_apps_clk_src.clkr,
	[BLSP2_QUP4_SPI_APPS_CLK_SRC] = &blsp2_qup4_spi_apps_clk_src.clkr,
	[BLSP2_UART1_APPS_CLK_SRC] = &blsp2_uart1_apps_clk_src.clkr,
	[BLSP2_UART2_APPS_CLK_SRC] = &blsp2_uart2_apps_clk_src.clkr,
	[CCI_CLK_SRC] = &cci_clk_src.clkr,
	[CSI0P_CLK_SRC] = &csi0p_clk_src.clkr,
	[CSI1P_CLK_SRC] = &csi1p_clk_src.clkr,
	[CSI2P_CLK_SRC] = &csi2p_clk_src.clkr,
	[CAMSS_GP0_CLK_SRC] = &camss_gp0_clk_src.clkr,
	[CAMSS_GP1_CLK_SRC] = &camss_gp1_clk_src.clkr,
	[MCLK0_CLK_SRC] = &mclk0_clk_src.clkr,
	[MCLK1_CLK_SRC] = &mclk1_clk_src.clkr,
	[MCLK2_CLK_SRC] = &mclk2_clk_src.clkr,
	[MCLK3_CLK_SRC] = &mclk3_clk_src.clkr,
	[CSI0PHYTIMER_CLK_SRC] = &csi0phytimer_clk_src.clkr,
	[CSI1PHYTIMER_CLK_SRC] = &csi1phytimer_clk_src.clkr,
	[CSI2PHYTIMER_CLK_SRC] = &csi2phytimer_clk_src.clkr,
	[CRYPTO_CLK_SRC] = &crypto_clk_src.clkr,
	[GP1_CLK_SRC] = &gp1_clk_src.clkr,
	[GP2_CLK_SRC] = &gp2_clk_src.clkr,
	[GP3_CLK_SRC] = &gp3_clk_src.clkr,
	[PDM2_CLK_SRC] = &pdm2_clk_src.clkr,
	[RBCPR_GFX_CLK_SRC] = &rbcpr_gfx_clk_src.clkr,
	[SDCC1_APPS_CLK_SRC] = &sdcc1_apps_clk_src.clkr,
	[SDCC1_ICE_CORE_CLK_SRC] = &sdcc1_ice_core_clk_src.clkr,
	[SDCC2_APPS_CLK_SRC] = &sdcc2_apps_clk_src.clkr,
	[USB30_MOCK_UTMI_CLK_SRC] = &usb30_mock_utmi_clk_src.clkr,
	[USB3_AUX_CLK_SRC] = &usb3_aux_clk_src.clkr,
	[GCC_APC0_DROOP_DETECTOR_GPLL0_CLK] = &gcc_apc0_droop_detector_gpll0_clk.clkr,
	[GCC_APC1_DROOP_DETECTOR_GPLL0_CLK] = &gcc_apc1_droop_detector_gpll0_clk.clkr,
	[GCC_BLSP1_QUP1_I2C_APPS_CLK] = &gcc_blsp1_qup1_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP1_SPI_APPS_CLK] = &gcc_blsp1_qup1_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP2_I2C_APPS_CLK] = &gcc_blsp1_qup2_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP2_SPI_APPS_CLK] = &gcc_blsp1_qup2_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP3_I2C_APPS_CLK] = &gcc_blsp1_qup3_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP3_SPI_APPS_CLK] = &gcc_blsp1_qup3_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP4_I2C_APPS_CLK] = &gcc_blsp1_qup4_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP4_SPI_APPS_CLK] = &gcc_blsp1_qup4_spi_apps_clk.clkr,
	[GCC_BLSP1_UART1_APPS_CLK] = &gcc_blsp1_uart1_apps_clk.clkr,
	[GCC_BLSP1_UART2_APPS_CLK] = &gcc_blsp1_uart2_apps_clk.clkr,
	[GCC_BLSP2_QUP1_I2C_APPS_CLK] = &gcc_blsp2_qup1_i2c_apps_clk.clkr,
	[GCC_BLSP2_QUP1_SPI_APPS_CLK] = &gcc_blsp2_qup1_spi_apps_clk.clkr,
	[GCC_BLSP2_QUP2_I2C_APPS_CLK] = &gcc_blsp2_qup2_i2c_apps_clk.clkr,
	[GCC_BLSP2_QUP2_SPI_APPS_CLK] = &gcc_blsp2_qup2_spi_apps_clk.clkr,
	[GCC_BLSP2_QUP3_I2C_APPS_CLK] = &gcc_blsp2_qup3_i2c_apps_clk.clkr,
	[GCC_BLSP2_QUP3_SPI_APPS_CLK] = &gcc_blsp2_qup3_spi_apps_clk.clkr,
	[GCC_BLSP2_QUP4_I2C_APPS_CLK] = &gcc_blsp2_qup4_i2c_apps_clk.clkr,
	[GCC_BLSP2_QUP4_SPI_APPS_CLK] = &gcc_blsp2_qup4_spi_apps_clk.clkr,
	[GCC_BLSP2_UART1_APPS_CLK] = &gcc_blsp2_uart1_apps_clk.clkr,
	[GCC_BLSP2_UART2_APPS_CLK] = &gcc_blsp2_uart2_apps_clk.clkr,
	[GCC_CAMSS_CCI_AHB_CLK] = &gcc_camss_cci_ahb_clk.clkr,
	[GCC_CAMSS_CCI_CLK] = &gcc_camss_cci_clk.clkr,
	[GCC_CAMSS_CPP_AHB_CLK] = &gcc_camss_cpp_ahb_clk.clkr,
	[GCC_CAMSS_CPP_AXI_CLK] = &gcc_camss_cpp_axi_clk.clkr,
	[GCC_CAMSS_CPP_CLK] = &gcc_camss_cpp_clk.clkr,
	[GCC_CAMSS_CSI0_AHB_CLK] = &gcc_camss_csi0_ahb_clk.clkr,
	[GCC_CAMSS_CSI0_CLK] = &gcc_camss_csi0_clk.clkr,
	[GCC_CAMSS_CSI0_CSIPHY_3P_CLK] = &gcc_camss_csi0_csiphy_3p_clk.clkr,
	[GCC_CAMSS_CSI0PHY_CLK] = &gcc_camss_csi0phy_clk.clkr,
	[GCC_CAMSS_CSI0PIX_CLK] = &gcc_camss_csi0pix_clk.clkr,
	[GCC_CAMSS_CSI0RDI_CLK] = &gcc_camss_csi0rdi_clk.clkr,
	[GCC_CAMSS_CSI1_AHB_CLK] = &gcc_camss_csi1_ahb_clk.clkr,
	[GCC_CAMSS_CSI1_CLK] = &gcc_camss_csi1_clk.clkr,
	[GCC_CAMSS_CSI1_CSIPHY_3P_CLK] = &gcc_camss_csi1_csiphy_3p_clk.clkr,
	[GCC_CAMSS_CSI1PHY_CLK] = &gcc_camss_csi1phy_clk.clkr,
	[GCC_CAMSS_CSI1PIX_CLK] = &gcc_camss_csi1pix_clk.clkr,
	[GCC_CAMSS_CSI1RDI_CLK] = &gcc_camss_csi1rdi_clk.clkr,
	[GCC_CAMSS_CSI2_AHB_CLK] = &gcc_camss_csi2_ahb_clk.clkr,
	[GCC_CAMSS_CSI2_CLK] = &gcc_camss_csi2_clk.clkr,
	[GCC_CAMSS_CSI2_CSIPHY_3P_CLK] = &gcc_camss_csi2_csiphy_3p_clk.clkr,
	[GCC_CAMSS_CSI2PHY_CLK] = &gcc_camss_csi2phy_clk.clkr,
	[GCC_CAMSS_CSI2PIX_CLK] = &gcc_camss_csi2pix_clk.clkr,
	[GCC_CAMSS_CSI2RDI_CLK] = &gcc_camss_csi2rdi_clk.clkr,
	[GCC_CAMSS_CSI_VFE0_CLK] = &gcc_camss_csi_vfe0_clk.clkr,
	[GCC_CAMSS_CSI_VFE1_CLK] = &gcc_camss_csi_vfe1_clk.clkr,
	[GCC_CAMSS_GP0_CLK] = &gcc_camss_gp0_clk.clkr,
	[GCC_CAMSS_GP1_CLK] = &gcc_camss_gp1_clk.clkr,
	[GCC_CAMSS_ISPIF_AHB_CLK] = &gcc_camss_ispif_ahb_clk.clkr,
	[GCC_CAMSS_JPEG0_CLK] = &gcc_camss_jpeg0_clk.clkr,
	[GCC_CAMSS_JPEG_AHB_CLK] = &gcc_camss_jpeg_ahb_clk.clkr,
	[GCC_CAMSS_JPEG_AXI_CLK] = &gcc_camss_jpeg_axi_clk.clkr,
	[GCC_CAMSS_MCLK0_CLK] = &gcc_camss_mclk0_clk.clkr,
	[GCC_CAMSS_MCLK1_CLK] = &gcc_camss_mclk1_clk.clkr,
	[GCC_CAMSS_MCLK2_CLK] = &gcc_camss_mclk2_clk.clkr,
	[GCC_CAMSS_MCLK3_CLK] = &gcc_camss_mclk3_clk.clkr,
	[GCC_CAMSS_MICRO_AHB_CLK] = &gcc_camss_micro_ahb_clk.clkr,
	[GCC_CAMSS_CSI0PHYTIMER_CLK] = &gcc_camss_csi0phytimer_clk.clkr,
	[GCC_CAMSS_CSI1PHYTIMER_CLK] = &gcc_camss_csi1phytimer_clk.clkr,
	[GCC_CAMSS_CSI2PHYTIMER_CLK] = &gcc_camss_csi2phytimer_clk.clkr,
	[GCC_CAMSS_AHB_CLK] = &gcc_camss_ahb_clk.clkr,
	[GCC_CAMSS_TOP_AHB_CLK] = &gcc_camss_top_ahb_clk.clkr,
	[GCC_CAMSS_VFE0_CLK] = &gcc_camss_vfe0_clk.clkr,
	[GCC_CAMSS_VFE_AHB_CLK] = &gcc_camss_vfe_ahb_clk.clkr,
	[GCC_CAMSS_VFE_AXI_CLK] = &gcc_camss_vfe_axi_clk.clkr,
	[GCC_CAMSS_VFE1_AHB_CLK] = &gcc_camss_vfe1_ahb_clk.clkr,
	[GCC_CAMSS_VFE1_AXI_CLK] = &gcc_camss_vfe1_axi_clk.clkr,
	[GCC_CAMSS_VFE1_CLK] = &gcc_camss_vfe1_clk.clkr,
	[GCC_DCC_CLK] = &gcc_dcc_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_MSS_CFG_AHB_CLK] = &gcc_mss_cfg_ahb_clk.clkr,
	[GCC_MSS_Q6_BIMC_AXI_CLK] = &gcc_mss_q6_bimc_axi_clk.clkr,
	[GCC_PCNOC_USB3_AXI_CLK] = &gcc_pcnoc_usb3_axi_clk.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_RBCPR_GFX_CLK] = &gcc_rbcpr_gfx_clk.clkr,
	[GCC_SDCC1_AHB_CLK] = &gcc_sdcc1_ahb_clk.clkr,
	[GCC_SDCC1_APPS_CLK] = &gcc_sdcc1_apps_clk.clkr,
	[GCC_SDCC1_ICE_CORE_CLK] = &gcc_sdcc1_ice_core_clk.clkr,
	[GCC_SDCC2_AHB_CLK] = &gcc_sdcc2_ahb_clk.clkr,
	[GCC_SDCC2_APPS_CLK] = &gcc_sdcc2_apps_clk.clkr,
	[GCC_USB30_MASTER_CLK] = &gcc_usb30_master_clk.clkr,
	[GCC_USB30_MOCK_UTMI_CLK] = &gcc_usb30_mock_utmi_clk.clkr,
	[GCC_USB30_SLEEP_CLK] = &gcc_usb30_sleep_clk.clkr,
	[GCC_USB3_AUX_CLK] = &gcc_usb3_aux_clk.clkr,
	[GCC_USB_PHY_CFG_AHB_CLK] = &gcc_usb_phy_cfg_ahb_clk.clkr,
	[GCC_VENUS0_AHB_CLK] = &gcc_venus0_ahb_clk.clkr,
	[GCC_VENUS0_AXI_CLK] = &gcc_venus0_axi_clk.clkr,
	[GCC_VENUS0_CORE0_VCODEC0_CLK] = &gcc_venus0_core0_vcodec0_clk.clkr,
	[GCC_VENUS0_VCODEC0_CLK] = &gcc_venus0_vcodec0_clk.clkr,
	[GCC_QUSB_REF_CLK] = &gcc_qusb_ref_clk.clkr,
	[GCC_USB_SS_REF_CLK] = &gcc_usb_ss_ref_clk.clkr,
	[GCC_USB3_PIPE_CLK] = &gcc_usb3_pipe_clk.clkr,
	[MDP_CLK_SRC] = &mdp_clk_src.clkr,
	[PCLK0_CLK_SRC] = &pclk0_clk_src.clkr,
	[BYTE0_CLK_SRC] = &byte0_clk_src.clkr,
	[ESC0_CLK_SRC] = &esc0_clk_src.clkr,
	[PCLK1_CLK_SRC] = &pclk1_clk_src.clkr,
	[BYTE1_CLK_SRC] = &byte1_clk_src.clkr,
	[ESC1_CLK_SRC] = &esc1_clk_src.clkr,
	[VSYNC_CLK_SRC] = &vsync_clk_src.clkr,
	[GCC_MDSS_AHB_CLK] = &gcc_mdss_ahb_clk.clkr,
	[GCC_MDSS_AXI_CLK] = &gcc_mdss_axi_clk.clkr,
	[GCC_MDSS_PCLK0_CLK] = &gcc_mdss_pclk0_clk.clkr,
	[GCC_MDSS_BYTE0_CLK] = &gcc_mdss_byte0_clk.clkr,
	[GCC_MDSS_ESC0_CLK] = &gcc_mdss_esc0_clk.clkr,
	[GCC_MDSS_PCLK1_CLK] = &gcc_mdss_pclk1_clk.clkr,
	[GCC_MDSS_BYTE1_CLK] = &gcc_mdss_byte1_clk.clkr,
	[GCC_MDSS_ESC1_CLK] = &gcc_mdss_esc1_clk.clkr,
	[GCC_MDSS_MDP_CLK] = &gcc_mdss_mdp_clk.clkr,
	[GCC_MDSS_VSYNC_CLK] = &gcc_mdss_vsync_clk.clkr,
	[GCC_OXILI_TIMER_CLK] = &gcc_oxili_timer_clk.clkr,
	[GCC_OXILI_GFX3D_CLK] = &gcc_oxili_gfx3d_clk.clkr,
	[GCC_OXILI_AON_CLK] = &gcc_oxili_aon_clk.clkr,
	[GCC_OXILI_AHB_CLK] = &gcc_oxili_ahb_clk.clkr,
	[GCC_BIMC_GFX_CLK] = &gcc_bimc_gfx_clk.clkr,
	[GCC_BIMC_GPU_CLK] = &gcc_bimc_gpu_clk.clkr,
	[GFX3D_CLK_SRC] = &gfx3d_clk_src.clkr,
};

static const struct qcom_reset_map gcc_msm8953_resets[] = {
	[GCC_QUSB2_PHY_BCR]	= { 0x4103C },
	[GCC_USB3_PHY_BCR]	= { 0x3F034 },
	[GCC_USB3PHY_PHY_BCR]	= { 0x3F03C },
	[GCC_USB_30_BCR]	= { 0x3F070 },
	[GCC_CAMSS_MICRO_BCR]	= { 0x56008 },
	[GCC_MSS_RESTART]	= { 0x71000 },
};

static const struct regmap_config gcc_msm8953_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x7fffc,
	.fast_io	= true,
};

static struct gdsc *gcc_msm8953_gdscs[] = {
	[USB30_GDSC] = &usb30_gdsc,
	[VENUS_GDSC] = &venus_gdsc,
	[MDSS_GDSC] = &mdss_gdsc,
	[JPEG_GDSC] = &jpeg_gdsc,
	[VFE_GDSC] = &vfe_gdsc,
	[OXILI_GX_GDSC] = &oxili_gx_gdsc,
	[OXILI_CX_GDSC] = &oxili_cx_gdsc,
};

static const struct qcom_cc_desc gcc_msm8953_desc = {
	.config = &gcc_msm8953_regmap_config,
	.clks = gcc_msm8953_clocks,
	.num_clks = ARRAY_SIZE(gcc_msm8953_clocks),
	.resets = gcc_msm8953_resets,
	.num_resets = ARRAY_SIZE(gcc_msm8953_resets),
	.gdscs = gcc_msm8953_gdscs,
	.num_gdscs = ARRAY_SIZE(gcc_msm8953_gdscs),
	.clk_hws = gcc_msm8953_hws,
	.num_clk_hws = ARRAY_SIZE(gcc_msm8953_hws),
};

#ifdef CONFIG_DEBUG_FS
static DEFINE_MUTEX(clk_debug_lock);
static DEFINE_SPINLOCK(local_clock_reg_lock);
static struct regmap *regmap;
static struct dentry *rootdir;

struct debug_mux_item {
	u16 value;
	const char *name;
	u16 mult;
	u32 mux_addr;
	u32 mux_val;
};

static struct debug_mux_item gcc_debug_mux_parents[] = {
	{ 0x16a, "apcs_c0_clk" , 16, 0x0b11101c, 0x000 },
	{ 0x16a, "apcs_c1_clk" , 16, 0x0b11101c, 0x100 },
	{ 0x16a, "apcs_cci_clk" , 16, 0x0b11101c, 0x200 },
	{ 0x000, "snoc_clk" },
	{ 0x001, "sysmmnoc_clk" },
	{ 0x008, "pcnoc_clk" },
	{ 0x15a, "bimc_clk" },
	{ 0x1b0, "ipa_clk" },
	{ 0x00d, "gcc_dcc_clk" },
	{ 0x00e, "gcc_pcnoc_usb3_axi_clk" },
	{ 0x010, "gcc_gp1_clk" },
	{ 0x011, "gcc_gp2_clk" },
	{ 0x012, "gcc_gp3_clk" },
	{ 0x01c, "gcc_apc0_droop_detector_gpll0_clk" },
	{ 0x01d, "gcc_camss_csi2phytimer_clk" },
	{ 0x01f, "gcc_apc1_droop_detector_gpll0_clk" },
	{ 0x02d, "gcc_bimc_gfx_clk" },
	{ 0x030, "gcc_mss_cfg_ahb_clk" },
	{ 0x031, "gcc_mss_q6_bimc_axi_clk" },
	{ 0x049, "gcc_qdss_dap_clk" },
	{ 0x050, "gcc_apss_tcu_async_clk" },
	{ 0x051, "gcc_mdp_tbu_clk" },
	{ 0x054, "gcc_venus_tbu_clk" },
	{ 0x05a, "gcc_vfe_tbu_clk" },
	{ 0x05b, "gcc_smmu_cfg_clk" },
	{ 0x05c, "gcc_jpeg_tbu_clk" },
	{ 0x060, "gcc_usb30_master_clk" },
	{ 0x061, "gcc_usb30_sleep_clk" },
	{ 0x062, "gcc_usb30_mock_utmi_clk" },
	{ 0x063, "gcc_usb_phy_cfg_ahb_clk" },
	{ 0x066, "gcc_usb3_pipe_clk" },
	{ 0x067, "gcc_usb3_aux_clk" },
	{ 0x068, "gcc_sdcc1_apps_clk" },
	{ 0x069, "gcc_sdcc1_ahb_clk" },
	{ 0x06a, "gcc_sdcc1_ice_core_clk" },
	{ 0x070, "gcc_sdcc2_apps_clk" },
	{ 0x071, "gcc_sdcc2_ahb_clk" },
	{ 0x088, "gcc_blsp1_ahb_clk" },
	{ 0x08a, "gcc_blsp1_qup1_spi_apps_clk" },
	{ 0x08b, "gcc_blsp1_qup1_i2c_apps_clk" },
	{ 0x08c, "gcc_blsp1_uart1_apps_clk" },
	{ 0x08e, "gcc_blsp1_qup2_spi_apps_clk" },
	{ 0x090, "gcc_blsp1_qup2_i2c_apps_clk" },
	{ 0x091, "gcc_blsp1_uart2_apps_clk" },
	{ 0x093, "gcc_blsp1_qup3_spi_apps_clk" },
	{ 0x094, "gcc_blsp1_qup3_i2c_apps_clk" },
	{ 0x095, "gcc_blsp1_qup4_spi_apps_clk" },
	{ 0x096, "gcc_blsp1_qup4_i2c_apps_clk" },
	{ 0x098, "gcc_blsp2_ahb_clk" },
	{ 0x09a, "gcc_blsp2_qup1_spi_apps_clk" },
	{ 0x09b, "gcc_blsp2_qup1_i2c_apps_clk" },
	{ 0x09c, "gcc_blsp2_uart1_apps_clk" },
	{ 0x09e, "gcc_blsp2_qup2_spi_apps_clk" },
	{ 0x0a0, "gcc_blsp2_qup2_i2c_apps_clk" },
	{ 0x0a1, "gcc_blsp2_uart2_apps_clk" },
	{ 0x0a3, "gcc_blsp2_qup3_spi_apps_clk" },
	{ 0x0a4, "gcc_blsp2_qup3_i2c_apps_clk" },
	{ 0x0a5, "gcc_blsp2_qup4_spi_apps_clk" },
	{ 0x0a6, "gcc_blsp2_qup4_i2c_apps_clk" },
	{ 0x0a8, "gcc_camss_ahb_clk" },
	{ 0x0a9, "gcc_camss_top_ahb_clk" },
	{ 0x0aa, "gcc_camss_micro_ahb_clk" },
	{ 0x0ab, "gcc_camss_gp0_clk" },
	{ 0x0ac, "gcc_camss_gp1_clk" },
	{ 0x0ad, "gcc_camss_mclk0_clk" },
	{ 0x0ae, "gcc_camss_mclk1_clk" },
	{ 0x0af, "gcc_camss_cci_clk" },
	{ 0x0b0, "gcc_camss_cci_ahb_clk" },
	{ 0x0b1, "gcc_camss_csi0phytimer_clk" },
	{ 0x0b2, "gcc_camss_csi1phytimer_clk" },
	{ 0x0b3, "gcc_camss_jpeg0_clk" },
	{ 0x0b4, "gcc_camss_jpeg_ahb_clk" },
	{ 0x0b5, "gcc_camss_jpeg_axi_clk" },
	{ 0x0b8, "gcc_camss_vfe0_clk" },
	{ 0x0b9, "gcc_camss_cpp_clk" },
	{ 0x0ba, "gcc_camss_cpp_ahb_clk" },
	{ 0x0bb, "gcc_camss_vfe_ahb_clk" },
	{ 0x0bc, "gcc_camss_vfe_axi_clk" },
	{ 0x0bf, "gcc_camss_csi_vfe0_clk" },
	{ 0x0c0, "gcc_camss_csi0_clk" },
	{ 0x0c1, "gcc_camss_csi0_ahb_clk" },
	{ 0x0c2, "gcc_camss_csi0phy_clk" },
	{ 0x0c3, "gcc_camss_csi0rdi_clk" },
	{ 0x0c4, "gcc_camss_csi0pix_clk" },
	{ 0x0c5, "gcc_camss_csi1_clk" },
	{ 0x0c6, "gcc_camss_csi1_ahb_clk" },
	{ 0x0c7, "gcc_camss_csi1phy_clk" },
	{ 0x0d0, "gcc_pdm_ahb_clk" },
	{ 0x0d2, "gcc_pdm2_clk" },
	{ 0x0d8, "gcc_prng_ahb_clk" },
	{ 0x0da, "gcc_mdss_byte1_clk" },
	{ 0x0db, "gcc_mdss_esc1_clk" },
	{ 0x0dc, "gcc_camss_csi0_csiphy_3p_clk" },
	{ 0x0dd, "gcc_camss_csi1_csiphy_3p_clk" },
	{ 0x0de, "gcc_camss_csi2_csiphy_3p_clk" },
	{ 0x0e0, "gcc_camss_csi1rdi_clk" },
	{ 0x0e1, "gcc_camss_csi1pix_clk" },
	{ 0x0e2, "gcc_camss_ispif_ahb_clk" },
	{ 0x0e3, "gcc_camss_csi2_clk" },
	{ 0x0e4, "gcc_camss_csi2_ahb_clk" },
	{ 0x0e5, "gcc_camss_csi2phy_clk" },
	{ 0x0e6, "gcc_camss_csi2rdi_clk" },
	{ 0x0e7, "gcc_camss_csi2pix_clk" },
	{ 0x0e9, "gcc_cpp_tbu_clk" },
	{ 0x0f0, "gcc_rbcpr_gfx_clk" },
	{ 0x0f8, "gcc_boot_rom_ahb_clk" },
	{ 0x138, "gcc_crypto_clk" },
	{ 0x139, "gcc_crypto_axi_clk" },
	{ 0x13a, "gcc_crypto_ahb_clk" },
	{ 0x157, "gcc_bimc_gpu_clk" },
	{ 0x168, "gcc_apss_ahb_clk" },
	{ 0x169, "gcc_apss_axi_clk" },
	{ 0x199, "gcc_vfe1_tbu_clk" },
	{ 0x1a0, "gcc_camss_csi_vfe1_clk" },
	{ 0x1a1, "gcc_camss_vfe1_clk" },
	{ 0x1a2, "gcc_camss_vfe1_ahb_clk" },
	{ 0x1a3, "gcc_camss_vfe1_axi_clk" },
	{ 0x1a4, "gcc_camss_cpp_axi_clk" },
	{ 0x1b8, "gcc_venus0_core0_vcodec0_clk" },
	{ 0x1bd, "gcc_camss_mclk2_clk" },
	{ 0x1bf, "gcc_camss_mclk3_clk" },
	{ 0x1e8, "gcc_oxili_aon_clk" },
	{ 0x1e9, "gcc_oxili_timer_clk" },
	{ 0x1ea, "gcc_oxili_gfx3d_clk" },
	{ 0x1eb, "gcc_oxili_ahb_clk" },
	{ 0x1f1, "gcc_venus0_vcodec0_clk" },
	{ 0x1f2, "gcc_venus0_axi_clk" },
	{ 0x1f3, "gcc_venus0_ahb_clk" },
	{ 0x1f6, "gcc_mdss_ahb_clk" },
	{ 0x1f7, "gcc_mdss_axi_clk" },
	{ 0x1f8, "gcc_mdss_pclk0_clk" },
	{ 0x1f9, "gcc_mdss_mdp_clk" },
	{ 0x1fa, "gcc_mdss_pclk1_clk" },
	{ 0x1fb, "gcc_mdss_vsync_clk" },
	{ 0x1fc, "gcc_mdss_byte0_clk" },
	{ 0x1fd, "gcc_mdss_esc0_clk" },
	{ 0x0ec, "wcnss_m_clk" },
};

static u32 run_measurement(unsigned int ticks,
		u32 ctl_reg, u32 status_reg)
{
	u32 regval;
	/* Stop counters and set the XO4 counter start value. */
	regmap_write(regmap, ctl_reg, ticks);

	/* Wait for timer to become ready. */
	do {
		cpu_relax();
		regmap_read(regmap, status_reg, &regval);
	} while ((regval & BIT(25)) != 0);

	/* Run measurement and wait for completion. */
	regmap_write(regmap, ctl_reg, BIT(20)|ticks);
	do {
		cpu_relax();
		regmap_read(regmap, status_reg, &regval);
	} while ((regval & BIT(25)) == 0);

	/* Return measured ticks. */
	return regval & GENMASK(24, 0);
}

struct measure_clk_data {
	u32 plltest_reg;
	u32 plltest_val;
	u32 xo_div4_cbcr;
	u32 ctl_reg;
	u32 status_reg;
};

static struct measure_clk_data debug_data = {
	.plltest_reg	= 0x7400C,
	.plltest_val	= 0x51A00,
	.xo_div4_cbcr	= 0x30034,
	.ctl_reg	= 0x74004,
	.status_reg	= 0x74008,
};

int gcc_measure_clk(void *data, u64 *val)
{
	struct debug_mux_item *item = (struct debug_mux_item*) data;
	unsigned long flags;
	u32 gcc_xo4_reg, regval;
	u64 raw_count_short, raw_count_full;
	u32 sample_ticks = 0x10000;
	u32 multiplier = item->mult ? item->mult : 1;
	u32 mux_reg = 0x74000;
	u32 enable_mask = BIT(16);

	if (item->mux_addr) {
		void __iomem *mux_mem = ioremap(item->mux_addr, 0x4);
		if (IS_ERR_OR_NULL(mux_mem))
			return PTR_ERR(mux_mem);
		writel_relaxed(item->mux_val, mux_mem);
		iounmap(mux_mem);
	}

	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	spin_lock_irqsave(&local_clock_reg_lock, flags);

	regmap_read(regmap, mux_reg, &regval);
	regval = item->value | enable_mask;
	regmap_write(regmap, mux_reg, regval);
	mb();
	udelay(1);

	/* Enable CXO/4 and RINGOSC branch. */
	if (regmap_read(regmap, debug_data.xo_div4_cbcr, &gcc_xo4_reg))
		goto fail;
	gcc_xo4_reg |= BIT(0); //CBCR_BRANCH_ENABLE_BIT
	regmap_write(regmap, debug_data.xo_div4_cbcr, gcc_xo4_reg);
	mb();

	/*
	 * The ring oscillator counter will not reset if the measured clock
	 * is not running.  To detect this, run a short measurement before
	 * the full measurement.  If the raw results of the two are the same
	 * then the clock must be off.
	 */

	/* Run a short measurement. (~1 ms) */
	raw_count_short = run_measurement(0x1000,
			debug_data.ctl_reg,
			debug_data.status_reg);
	/* Run a full measurement. (~14 ms) */
	raw_count_full = run_measurement(sample_ticks,
			debug_data.ctl_reg,
			debug_data.status_reg);

	gcc_xo4_reg &= ~BIT(0); // CBCR_BRANCH_ENABLE_BIT
	regmap_write(regmap, debug_data.xo_div4_cbcr, gcc_xo4_reg);

	/* Return 0 if the clock is off. */
	if (raw_count_full == raw_count_short) {
		*val = 0;
	} else {
		/* Compute rate in Hz. */
		raw_count_full = ((raw_count_full * 10) + 15) * 4800000;
		do_div(raw_count_full, ((sample_ticks * 10) + 35));
		*val = (raw_count_full * multiplier);
	}
	regmap_write(regmap, debug_data.plltest_reg, debug_data.plltest_val);

fail:
	regmap_read(regmap, mux_reg, &regval);
	/* clear and set post divider bits */
	regval &= ~enable_mask;
	regmap_write(regmap, mux_reg, regval);
	spin_unlock_irqrestore(&local_clock_reg_lock, flags);

	return 0;
}
EXPORT_SYMBOL(gcc_measure_clk);
DEFINE_DEBUGFS_ATTRIBUTE(clk_rate_fops, gcc_measure_clk, NULL, "%llu\n");

void gcc_measure_all(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(gcc_debug_mux_parents) ; i ++ ) {
		u64 freq;
		gcc_measure_clk(&gcc_debug_mux_parents[i], &freq);
		printk("clock:%s frequency:%llu\n", gcc_debug_mux_parents[i].name, freq);
	}
}
EXPORT_SYMBOL(gcc_measure_all);
#endif

static int gcc_msm8953_probe(struct platform_device *pdev)
{
	int ret = 0;

	regmap = qcom_cc_map(pdev, &gcc_msm8953_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);


	ret = qcom_cc_really_probe(pdev, &gcc_msm8953_desc, regmap);
#ifdef CONFIG_DEBUG_FS
	if (!ret) {
		int i = 0;
		rootdir = debugfs_create_dir("clk-measure", NULL);
		if (IS_ERR_OR_NULL(rootdir))
			return ret;
		for (i = 0; i < ARRAY_SIZE(gcc_debug_mux_parents); i++) {
			debugfs_create_file(gcc_debug_mux_parents[i].name,
					0660,
					rootdir,
					&gcc_debug_mux_parents[i],
					&clk_rate_fops);

		}
	}
#endif
	{
#define GX_DOMAIN_MISC	0x5B00C
		void __iomem *base = ioremap(0x1800000, 0x80000);
		u32 regval;
		regval = readl_relaxed(base + GX_DOMAIN_MISC);
		regval &= ~BIT(0);
		writel_relaxed(regval, base + GX_DOMAIN_MISC);
		iounmap(base);

	}
	return ret;
}

static const struct of_device_id gcc_msm8953_match_table[] = {
	{ .compatible = "qcom,gcc-msm8953" },
	{},
};

static struct platform_driver gcc_msm8953_driver = {
	.probe = gcc_msm8953_probe,
	.driver = {
		.name = "gcc-msm8953",
		.of_match_table = gcc_msm8953_match_table,
		.owner = THIS_MODULE,
	},
};

static int __init gcc_msm8953_init(void)
{
	return platform_driver_register(&gcc_msm8953_driver);
}
arch_initcall(gcc_msm8953_init);

static void __exit gcc_msm8953_exit(void)
{
	platform_driver_unregister(&gcc_msm8953_driver);
}
module_exit(gcc_msm8953_exit);
