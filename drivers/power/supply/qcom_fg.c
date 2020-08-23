/* Copyright (c) 2020, The Linux Foundation. All rights reserved. */

#define pr_fmt(fmt)	"qcom-fg: %s: " fmt, __func__

#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/types.h>

/* Register offsets */

/* Interrupt offsets */
#define INT_RT_STS(base)			(base + 0x10)
#define INT_EN_CLR(base)			(base + 0x16)

/* RAM resister offsets */
#define RAM_OFFSET				0x400

/* Bit/Mask definitions */
#define FULL_PERCENT				0xFF
#define MAX_TRIES_SOC				5
#define MA_MV_BIT_RES				39
#define MSB_SIGN				BIT(7)
#define IBAT_VBAT_MASK				0x7f
#define NO_OTP_PROF_RELOAD			BIT(6)
#define REDO_FIRST_ESTIMATE			BIT(3)
#define RESTART_GO				BIT(0)
#define THERM_DELAY_MASK			0xe0

/* Registers */
#define BATT_CAPACITY_REG(chip) 		(chip->soc_base + 0x09)
#define SOC_BOOT_MODE_REG(chip)			(chip->soc_base + 0x50)
#define SOC_RESTART_REG(chip)			(chip->soc_base + 0x51)
#define SOC_ALG_STATUS_REG(chip)		(chip->soc_base + 0xcf)
#define SOC_FG_RESET_REG(chip)			(chip->soc_base + 0xf3)

#define MEM_INTF_CFG(chip)			(chip->mem_base + 0x50)
#define MEM_INTF_CTL(chip)			(chip->mem_base + 0x51)
#define MEM_INTF_IMA_CFG(chip)			(chip->mem_base + 0x52)
#define MEM_INTF_IMA_OPR_STS(chip)		(chip->mem_base + 0x54)
#define MEM_INTF_IMA_EXP_STS(chip)		(chip->mem_base + 0x55)
#define MEM_INTF_IMA_HW_STS(chip)		(chip->mem_base + 0x56)
#define MEM_INTF_BEAT_COUNT(chip)		(chip->mem_base + 0x57)
#define MEM_INTF_IMA_ERR_STS(chip)		(chip->mem_base + 0x5f)
#define MEM_INTF_IMA_BYTE_EN(chip)		(chip->mem_base + 0x60)
#define MEM_INTF_ADDR_LSB(chip)			(chip->mem_base + 0x61)
#define MEM_INTF_WR_DATA0(chip)			(chip->mem_base + 0x63)
#define MEM_INTF_RD_DATA0(chip)			(chip->mem_base + 0x67)
#define MEM_INTF_DMA_STS(chip)			(chip->mem_base + 0x70)
#define MEM_INTF_DMA_LOG(chip)			(chip->mem_base + 0x71)

/* Gen3 FG specific: */
#define BATT_SOC_RESTART(chip)			(chip->batt_base + 0x48)
#define BATT_INFO_THERM_C1(chip)		(chip->batt_base + 0x5c)
#define BATT_INFO_VBATT_LSB(chip)		(chip->batt_base + 0xa0)
#define BATT_INFO_VBATT_MSB(chip)		(chip->batt_base + 0xa1)
#define BATT_INFO_IBATT_LSB(chip)		(chip->batt_base + 0xa2)
#define BATT_INFO_IBATT_MSB(chip)		(chip->batt_base + 0xa3)
#define BATT_INFO_BATT_TEMP_LSB(chip)		(chip->batt_base + 0x50)
#define BATT_INFO_BATT_TEMP_MSB(chip)		(chip->batt_base + 0x51)
#define BATT_INFO_JEITA_COLD(chip)		(chip->batt_base + 0x62)
#define BATT_INFO_JEITA_COOL(chip)		(chip->batt_base + 0x63)
#define BATT_INFO_JEITA_WARM(chip)		(chip->batt_base + 0x64)
#define BATT_INFO_JEITA_HOT(chip)		(chip->batt_base + 0x65)
/* Gen3 v2 specific */
#define BATT_INFO_ESR_PULL_DN_CFG(chip)		(chip->batt_base + 0x69)
#define BATT_INFO_ESR_FAST_CRG_CFG(chip)	(chip->batt_base + 0x6A)

/* BATT_INFO_ESR_FAST_CRG_CFG */
#define ESR_FAST_CRG_IVAL_MASK			GENMASK(3, 1)
#define ESR_FCC_300MA				0x0
#define ESR_FCC_600MA				0x1
#define ESR_FCC_1A				0x2
#define ESR_FCC_2A				0x3
#define ESR_FCC_3A				0x4
#define ESR_FCC_4A				0x5
#define ESR_FCC_5A				0x6
#define ESR_FCC_6A				0x7
#define ESR_FAST_CRG_CTL_EN_BIT			BIT(0)
#define BATT_TEMP_LSB_MASK			GENMASK(7, 0)
#define BATT_TEMP_MSB_MASK			GENMASK(2, 0)

enum pmic {
	PMI8950,
	PMI8998_V1,
	PMI8998_V2
};

enum wa_flags {
	PMI8998_V1_REV_WA = BIT(0),
	PMI8998_V2_REV_WA = BIT(1),
};

enum fg_sram_param_id {
	FG_DATA_BATT_TEMP = 0,
	FG_DATA_CHARGE,
	FG_DATA_OCV,
	FG_DATA_VOLTAGE,
	FG_DATA_CURRENT,
	FG_DATA_BATT_SOC,
	FG_DATA_BATT_ESR,
	FG_DATA_BATT_ESR_REG,
	FG_DATA_FULL_SOC,
	FG_DATA_CYCLE_COUNT,
	FG_DATA_VINT_ERR,
	FG_DATA_CPRED_VOLTAGE,
	FG_DATA_CHARGE_COUNTER,
	FG_DATA_ACT_CAP,
	FG_DATA_NOM_CAP,
	FG_PARAM_ACTUAL_CAP,
	FG_PARAM_ACTUAL_CHARGE,
	FG_PARAM_MONOTONIC_SOC,
	FG_SETTING_RSLOW_CHG,
	FG_SETTING_RSLOW_DISCHG,
	FG_SETTING_THERMAL_COEFFS,
	//Settings below this have corresponding dt entries
	FG_SETTING_TERM_CURRENT,
	FG_SETTING_SYS_TERM_CURRENT,
	FG_SETTING_CHG_TERM_CURRENT,
	FG_SETTING_CHG_TERM_BASE_CURRENT,
	FG_SETTING_CUTOFF_VOLT,
	FG_SETTING_EMPTY_VOLT,
	FG_SETTING_MAX_VOLT,
	FG_SETTING_BATT_LOW,
	FG_SETTING_CONST_CHARGE_VOLT_THR, //previsouly called FG_SETTING_BATT_FULL
	FG_SETTING_RECHARGE_THR,
	FG_SETTING_RECHARGE_VOLT_THR,
	FG_SETTING_DELTA_MSOC,
	FG_SETTING_DELTA_BSOC,
	FG_SETTING_BCL_LM_THR,
	FG_SETTING_BCL_MH_THR,
	FG_PROFILE_INTEGRITY,
	FG_SETTING_ESR_TIMER_DISCHG_MAX,
	FG_SETTING_ESR_TIMER_DISCHG_INIT,
	FG_SETTING_ESR_TIMER_CHG_MAX,
	FG_SETTING_ESR_TIMER_CHG_INIT,
	FG_SETTING_ESR_TIGHT_FILTER,
	FG_SETTING_ESR_BROAD_FILTER,
	FG_SETTING_ESR_PULSE_THR,
	FG_SETTING_KI_COEFF_LOW_DISCHG,
	FG_SETTING_KI_COEFF_MED_DISCHG,
	FG_SETTING_KI_COEFF_HI_DISCHG,
	FG_SETTING_KI_COEFF_DISCHG,
	FG_SETTING_KI_COEFF_FULL_SOC,
	FG_PARAM_MAX
};

enum sram_param_type {
	SRAM_PARAM,
	MEM_IF_PARAM,
	BATT_BASE_PARAM,
	SOC_BASE_PARAM,
};

struct fg_sram_param {
	u16	address;
	u8	offset;
	unsigned int length;

	u8 type: 2;
	u8 wa_flags: 6;
	/* the conversion stuff */
	int numrtr;
	int denmtr;
	int val_offset;
	void (*encode)(struct fg_sram_param sp, int val, u8 *buf);
	int (*decode)(struct fg_sram_param sp, u8* val);
};

static int fg_decode_voltage_15b(struct fg_sram_param sp, u8 *val);
static int fg_decode_value_16b(struct fg_sram_param sp, u8 *val);
static int fg_decode_temperature(struct fg_sram_param sp, u8 *val);
static int fg_decode_current(struct fg_sram_param sp, u8 *val);
static int fg_decode_float(struct fg_sram_param sp, u8 *val);
static int fg_decode_default(struct fg_sram_param sp, u8 *val);
static int fg_decode_cc_soc_pmi8950(struct fg_sram_param sp, u8 *value);
static int fg_decode_cc_soc_pmi8998(struct fg_sram_param sp, u8 *value);
static int fg_decode_value_le(struct fg_sram_param sp, u8 *val);

static void fg_encode_voltage(struct fg_sram_param sp, int val_mv, u8 *buf);
static void fg_encode_current(struct fg_sram_param sp, int val_ma, u8 *buf);
static void fg_encode_float(struct fg_sram_param sp, int val_ma, u8 *buf);
static void fg_encode_roundoff(struct fg_sram_param sp, int val_ma, u8 *buf);
static void fg_encode_bcl(struct fg_sram_param sp, int val, u8 *buf);
static void fg_encode_voltcmp8(struct fg_sram_param sp, int val, u8 *buf);
static void fg_encode_adc(struct fg_sram_param sp, int val, u8 *buf);
static void fg_encode_default(struct fg_sram_param sp, int val, u8 *buf);

#define PMI8950_LSB_16B_NUMRTR	1000
#define PMI8950_LSB_16B_DENMTR	152587
#define FULL_PERCENT_3B		0xffffff
#define MICRO_UNIT		1000000ULL

#define FG_SRAM_PARAM_DEF(_name, addr, off, len, num, den, val_off,\
		enc, dec) \
	[FG_##_name] = { \
		.address	= addr,\
		.offset		= off,\
		.length		= len,\
		.numrtr		= num,\
		.denmtr		= den,\
		.encode		= enc,\
		.decode		= dec,\
		.val_offset	= val_off,\
	}

static struct fg_sram_param fg_params_pmi8950[FG_PARAM_MAX] = {
	FG_SRAM_PARAM_DEF(DATA_BATT_TEMP, 0x550, 2, 2, 1000, 625, -2730, NULL,
			fg_decode_value_16b),
	FG_SRAM_PARAM_DEF(DATA_CHARGE, 0x570, 0, 4, FULL_PERCENT_3B,
			10000, 0, NULL, fg_decode_current),
	FG_SRAM_PARAM_DEF(DATA_OCV, 0x588, 3, 2, PMI8950_LSB_16B_NUMRTR,
			PMI8950_LSB_16B_DENMTR, 0, NULL, fg_decode_value_16b),
	FG_SRAM_PARAM_DEF(DATA_VOLTAGE, 0x5cc, 1, 2, PMI8950_LSB_16B_NUMRTR,
			PMI8950_LSB_16B_DENMTR, 0, NULL, fg_decode_value_16b),
	FG_SRAM_PARAM_DEF(DATA_CPRED_VOLTAGE, 0x540, 0, 2,
			PMI8950_LSB_16B_NUMRTR, PMI8950_LSB_16B_DENMTR, 0, NULL,
			fg_decode_value_16b),
	FG_SRAM_PARAM_DEF(DATA_CURRENT, 0x5cc, 3, 2, PMI8950_LSB_16B_NUMRTR,
			PMI8950_LSB_16B_DENMTR, 0, NULL, fg_decode_current),
	FG_SRAM_PARAM_DEF(DATA_BATT_ESR, 0x544, 2, 2, 0, 0, 0, NULL,
			fg_decode_float),
	FG_SRAM_PARAM_DEF(DATA_ACT_CAP,
			0x5e4, 0, 2, 0, 0, 0, NULL, NULL),
	FG_SRAM_PARAM_DEF(DATA_BATT_SOC, 0x56c, 1, 3, FULL_PERCENT_3B,
			10000, 0, NULL, fg_decode_value_16b),
	FG_SRAM_PARAM_DEF(DATA_BATT_ESR_REG, 0x4f4, 2, 2, 0, 0, 0, NULL,
			fg_decode_float),
	FG_SRAM_PARAM_DEF(DATA_NOM_CAP, 0x4f4, 0, 2, 1000, 0, 0, NULL,
			fg_decode_value_le),
	FG_SRAM_PARAM_DEF(DATA_CYCLE_COUNT, 0x5e8, 0, 2, 0, 0, 0, NULL,
			fg_decode_value_le),
	FG_SRAM_PARAM_DEF(PARAM_ACTUAL_CAP, 0x578, 2, 2, 0, 0, 0,
			fg_encode_default, NULL),
	FG_SRAM_PARAM_DEF(PARAM_ACTUAL_CHARGE, 0x578, 0, 2, 0, 0, 0,
			fg_encode_float, NULL),
	FG_SRAM_PARAM_DEF(DATA_CHARGE_COUNTER, 0x5bc, 3, 4, 0, 0, 0,
			NULL, fg_decode_cc_soc_pmi8950),
	FG_SRAM_PARAM_DEF(SETTING_TERM_CURRENT, 0x40c,
			2, 2, PMI8950_LSB_16B_NUMRTR * 1000,
			PMI8950_LSB_16B_DENMTR, 0, fg_encode_default, NULL),
	FG_SRAM_PARAM_DEF(SETTING_CHG_TERM_CURRENT,
			0x4f8, 2, 2, -PMI8950_LSB_16B_NUMRTR * 1000,
			PMI8950_LSB_16B_DENMTR, 0, fg_encode_current, NULL),
	FG_SRAM_PARAM_DEF(SETTING_CUTOFF_VOLT,
			0x40c, 0, 2, PMI8950_LSB_16B_NUMRTR * 1000,
			PMI8950_LSB_16B_DENMTR, 0, fg_encode_current, NULL),
	FG_SRAM_PARAM_DEF(SETTING_EMPTY_VOLT, 0x458, 3, 1, 1, 9766, 0,
			fg_encode_voltcmp8, NULL),
	FG_SRAM_PARAM_DEF(SETTING_BATT_LOW, 0x458, 0, 1,
			512, 1000, 0, fg_encode_roundoff, NULL),
	FG_SRAM_PARAM_DEF(SETTING_DELTA_MSOC, 0x450, 3, 1,
			255, 100, 0, fg_encode_roundoff, NULL),
	FG_SRAM_PARAM_DEF(SETTING_RECHARGE_THR, 0x45c, 1, 1,
			256, 100, 0, fg_encode_default, NULL),
	FG_SRAM_PARAM_DEF(SETTING_BCL_LM_THR, 0x47c, 2, 1,
			100, 976, 0, fg_encode_bcl, NULL),
	FG_SRAM_PARAM_DEF(SETTING_BCL_MH_THR, 0x47c, 3, 1,
			100, 976, 0, fg_encode_bcl, NULL),
	FG_SRAM_PARAM_DEF(SETTING_CONST_CHARGE_VOLT_THR,
			0x4f4, 0, 2, 32768, 5000, 0, fg_encode_adc, NULL),
	FG_SRAM_PARAM_DEF(SETTING_RSLOW_CHG, 0x514, 2, 2, 0, 0, 0,
			fg_encode_float, fg_decode_float),
	FG_SRAM_PARAM_DEF(SETTING_RSLOW_DISCHG, 0x514, 0, 2, 0, 0, 0,
			fg_encode_float, fg_decode_float),
	//TODO: change to setting?
	[FG_DATA_VINT_ERR] = {
		.address	= 0x560,
		.offset		= 0,
		.length		= 4,
		.decode		= fg_decode_value_16b,
		.encode		= fg_encode_default
	},
	[FG_SETTING_THERMAL_COEFFS] = {
		.address	= 0x444,
		.offset		= 2,
		.length		= 0,
	},
	FG_SRAM_PARAM_DEF(PARAM_MONOTONIC_SOC,
			0x574, 2, 2, 0, 0, 0, NULL, NULL),
	FG_SRAM_PARAM_DEF(PROFILE_INTEGRITY,
			0x53c, 0, 1, 0, 0, 0, NULL, NULL),
};

/*
 * PMI8998 is different from PMI8950 and all previous hardware
 * It likes to write and read some values from the fg_soc, instead
 * of reading from the sram, like previous hardware does.
 * For such values, a dummy name value is put here to get the value from
 * the dt and use it. However addresses are defined separately, as they
 * depend on runtime values
 */

#define PMI8998_V1_LSB_15B_NUMRTR	1000
#define PMI8998_V1_LSB_15B_DENMTR	244141
#define PMI8998_V1_LSB_16B_NUMRTR	1000
#define PMI8998_V1_LSB_16B_DENMTR	390625
static struct fg_sram_param fg_params_pmi8998_v1[FG_PARAM_MAX] = {
	[FG_DATA_BATT_TEMP] = {
		.address	= 0x50,
		.type		= BATT_BASE_PARAM,
		.length		= 2,
		.numrtr		= 4,
		.denmtr		= 10,		//Kelvin to DeciKelvin
		.val_offset	= -2730,	//DeciKelvin to DeciDegc
		.decode		= fg_decode_temperature
	},
	[FG_DATA_BATT_SOC] = {
		.address	= 91,
		.offset		= 0,
		.length		= 4,
		.decode		= fg_decode_default,
	},
	[FG_DATA_NOM_CAP] = {
		.address	= 58,
		.offset		= 0,
		.length		= 2,
		.decode		= fg_decode_value_le
	},
	[FG_DATA_FULL_SOC] = {
		.address	= 93,
		.offset		= 2,
		.length		= 2,
		.decode		= fg_decode_default,
	},
	[FG_DATA_VOLTAGE] = {
		.address	= 0xa0,
		.type		= BATT_BASE_PARAM,
		.length		= 2,
		.numrtr		= 1000,
		.denmtr		= 122070,
		.wa_flags	= PMI8998_V1_REV_WA,
		.decode		= fg_decode_value_16b,
	},
	[FG_DATA_CPRED_VOLTAGE] = {
		.address	= 97,
		.offset		= 0,
		.length		= 2,
		.numrtr		= PMI8998_V1_LSB_15B_NUMRTR,
		.denmtr		= PMI8998_V1_LSB_15B_DENMTR,
		.decode		= fg_decode_voltage_15b,
	},
	[FG_DATA_OCV] = {
		.address	= 97,
		.offset		= 2,
		.length		= 2,
		.numrtr		= PMI8998_V1_LSB_15B_NUMRTR,
		.denmtr		= PMI8998_V1_LSB_15B_DENMTR,
		.decode		= fg_decode_voltage_15b,
	},
	[FG_DATA_BATT_ESR] = {
		.address	= 99,
		.offset		= 0,
		.length		= 2,
		.numrtr		= PMI8998_V1_LSB_15B_NUMRTR,
		.denmtr		= PMI8998_V1_LSB_15B_DENMTR,
		.decode		= fg_decode_voltage_15b,
	},
	[FG_DATA_CHARGE] = {
		.address	= 95,
		.offset		= 0,
		.length		= 4,
		.numrtr		= 1,
		.denmtr		= 1,
		.decode		= fg_decode_cc_soc_pmi8998,
	},
	[FG_DATA_CHARGE_COUNTER] = {
		.address	= 96,
		.offset		= 0,
		.length		= 4,
		.numrtr		= 1,
		.denmtr		= 1,
		.decode		= fg_decode_cc_soc_pmi8998,
	},
	[FG_DATA_ACT_CAP] = {
		.address	= 117,
		.offset		= 0,
		.length		= 2,
		.decode		= fg_decode_default,
	},
	[FG_DATA_NOM_CAP] = {
		.address	= 54,
		.offset		= 0,
		.length		= 2,
		.numrtr		= 1000,
		.decode		= fg_decode_value_le,
	},
	[FG_DATA_CYCLE_COUNT] = {
		.address	= 75,
		.offset		= 0,
		.length		= 2,
		.decode		= fg_decode_value_le
	},
	[FG_DATA_CURRENT] = {
		.address	= 0xa2,
		.length		= 2,
		.type		= BATT_BASE_PARAM,
		.wa_flags	= PMI8998_V1_REV_WA,
		.numrtr		= 1000,
		.denmtr		= 488281,
		.decode 	= fg_decode_current,
	},
	[FG_SETTING_CUTOFF_VOLT] = {
		.address	= 5,
		.offset		= 0,
		.length		= 2,
		.numrtr		= PMI8998_V1_LSB_15B_NUMRTR * 1000,
		.denmtr		= PMI8998_V1_LSB_15B_DENMTR,
		.encode		= fg_encode_voltage,
	},
	[FG_SETTING_EMPTY_VOLT] = {
		.address	= 15,
		.offset		= 0,
		.length		= 1,
		.numrtr		= PMI8998_V1_LSB_16B_NUMRTR * 1000,
		.denmtr		= PMI8998_V1_LSB_16B_DENMTR,
		.val_offset	= -2500,
		.encode		= fg_encode_voltage,
	},
	[FG_SETTING_BATT_LOW] = {
		.address	= 15,
		.offset		= 1,
		.length		= 1,
		.numrtr		= PMI8998_V1_LSB_16B_NUMRTR * 1000,
		.denmtr		= PMI8998_V1_LSB_16B_DENMTR,
		.val_offset	= -2500,
		.encode		= fg_encode_voltage,
	},
	[FG_SETTING_CONST_CHARGE_VOLT_THR] = {
		.address	= 7,
		.offset		= 0,
		.length		= 2,
		.numrtr		= PMI8998_V1_LSB_15B_NUMRTR,
		.denmtr		= PMI8998_V1_LSB_15B_DENMTR,
		.encode		= fg_encode_voltage,
	},
	[FG_SETTING_TERM_CURRENT] = {
		.address	= 4,
		.offset		= 0,
		.length		= 3,
		.numrtr		= 1000000,
		.denmtr		= 122070,
		.encode		= fg_encode_current,
	},
	[FG_SETTING_SYS_TERM_CURRENT] = {
		.address	= 6,
		.offset		= 0,
		.length		= 3,
		.numrtr		= 100000,
		.denmtr		= 122070,
		.encode		= fg_encode_current,
	},
	[FG_SETTING_CHG_TERM_CURRENT] = {
		.address	= 14,
		.offset		= 1,
		.length		= 1,
		.numrtr		= PMI8998_V1_LSB_16B_NUMRTR * 1000,
		.denmtr		= PMI8998_V1_LSB_16B_DENMTR,
		.encode		= fg_encode_current,
	},
	[FG_SETTING_DELTA_BSOC] = {
		.address	= 13,
		.offset		= 2,
		.length		= 1,
		.numrtr		= 2048,
		.denmtr		= 100,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_DELTA_MSOC] = {
		.address	= 12,
		.offset		= 3,
		.length		= 1,
		.numrtr		= 2048,
		.denmtr		= 100,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_RECHARGE_THR] = {
		.address	= 14,
		.offset		= 0,
		.length		= 1,
		.numrtr		= 256,
		.denmtr		= 100,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_RSLOW_DISCHG] = {
		.address	= 34,
		.offset		= 0,
		.length		= 1,
	},
	[FG_SETTING_RSLOW_CHG] = {
		.address	= 51,
		.offset		= 0,
		.length		= 1,
	},
	[FG_SETTING_ESR_TIMER_DISCHG_MAX] = {
		.address	= 17,
		.offset		= 0,
		.length		= 2,
		.numrtr		= 1,
		.denmtr		= 1,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_ESR_TIMER_DISCHG_INIT] = {
		.address	= 17,
		.offset		= 2,
		.length		= 2,
		.numrtr		= 1,
		.denmtr		= 1,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_ESR_TIMER_CHG_MAX] = {
		.address	= 18,
		.offset		= 0,
		.length		= 2,
		.numrtr		= 1,
		.denmtr		= 1,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_ESR_TIMER_CHG_INIT] = {
		.address	= 18,
		.offset		= 2,
		.length		= 2,
		.numrtr		= 1,
		.denmtr		= 1,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_ESR_PULSE_THR] = {
		.address	= 2,
		.offset		= 3,
		.length		= 1,
		.numrtr		= 100000,
		.denmtr		= 390625,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_ESR_TIGHT_FILTER] = {
		.address	= 8,
		.offset		= 0,
		.length		= 1,
		.numrtr		= 512,
		.denmtr		= 1000000,
		.encode		= fg_encode_default
	},
	[FG_SETTING_ESR_BROAD_FILTER] = {
		.address	= 8,
		.offset		= 1,
		.length		= 1,
		.numrtr		= 512,
		.denmtr		= 1000000,
		.encode		= fg_encode_default
	},
	[FG_SETTING_KI_COEFF_LOW_DISCHG] = {
		.address	= 10,
		.offset		= 2,
		.length		= 1,
		.numrtr		= 1000,
		.denmtr		= 244141,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_KI_COEFF_MED_DISCHG] = {
		.address	= 9,
		.length		= 1,
		.offset		= 3,
		.numrtr		= 1000,
		.denmtr		= 244141,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_KI_COEFF_HI_DISCHG] = {
		.address	= 10,
		.length		= 1,
		.offset		= 0,
		.numrtr		= 1000,
		.denmtr		= 244141,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_KI_COEFF_FULL_SOC] = {
		.address	= 12,
		.length		= 1,
		.offset		= 2,
		.numrtr		= 1000,
		.denmtr		= 244141,
		.encode		= fg_encode_default,
	},
	[FG_PARAM_MONOTONIC_SOC] = {
		.address	= 94,
		.offset		= 2,
		.length		= 2,
	},
	[FG_PROFILE_INTEGRITY] = {
		.address	= 79,
		.offset		= 1,
		.length		= 3,
	},
};

static struct fg_sram_param fg_params_pmi8998_v2[FG_PARAM_MAX] = {
	[FG_DATA_BATT_TEMP] = {
		.address	= 0x50,
		.type		= BATT_BASE_PARAM,
		.length		= 2,
		.numrtr		= 4,
		.denmtr		= 10,		//Kelvin to DeciKelvin
		.val_offset	= -2730,	//DeciKelvin to DeciDegc
		.decode		= fg_decode_temperature
	},
	[FG_DATA_BATT_SOC] = {
		.address	= 91,
		.offset		= 0,
		.length		= 4,
		.decode		= fg_decode_default,
	},
	[FG_DATA_FULL_SOC] = {
		.address	= 93,
		.offset		= 2,
		.length		= 2,
		.decode		= fg_decode_default,
	},
	[FG_DATA_VOLTAGE] = {
		.address	= 0xa0,
		.type		= BATT_BASE_PARAM,
		.length		= 2,
		.numrtr		= 122070,
		.denmtr		= 1000,
		.wa_flags	= PMI8998_V2_REV_WA,
		.decode		= fg_decode_value_16b,
	},
	[FG_DATA_CPRED_VOLTAGE] = {
		.address	= 97,
		.offset		= 0,
		.length		= 2,
		.numrtr		= PMI8998_V1_LSB_15B_NUMRTR,
		.denmtr		= PMI8998_V1_LSB_15B_DENMTR,
		.decode		= fg_decode_voltage_15b,
	},
	[FG_DATA_OCV] = {
		.address	= 97,
		.offset		= 2,
		.length		= 2,
		.numrtr		= PMI8998_V1_LSB_15B_NUMRTR,
		.denmtr		= PMI8998_V1_LSB_15B_DENMTR,
		.decode		= fg_decode_voltage_15b,
	},
	[FG_DATA_BATT_ESR] = {
		.address	= 99,
		.offset		= 0,
		.length		= 2,
		.numrtr		= PMI8998_V1_LSB_15B_NUMRTR,
		.denmtr		= PMI8998_V1_LSB_15B_DENMTR,
		.decode		= fg_decode_voltage_15b,
	},
	[FG_DATA_CHARGE] = {
		.address	= 95,
		.offset		= 0,
		.length		= 4,
		.numrtr		= 1,
		.denmtr		= 1,
		.decode		= fg_decode_cc_soc_pmi8998,
	},
	[FG_DATA_CHARGE_COUNTER] = {
		.address	= 96,
		.offset		= 0,
		.length		= 4,
		.numrtr		= 1,
		.denmtr		= 1,
		.decode		= fg_decode_cc_soc_pmi8998,
	},
	[FG_DATA_ACT_CAP] = {
		.address	= 117,
		.offset		= 0,
		.length		= 2,
		.decode		= fg_decode_default,
	},
	[FG_DATA_NOM_CAP] = {
		.address	= 74,
		.offset		= 0,
		.length		= 2,
		.numrtr		= 1000,
		.decode		= fg_decode_value_le
	},
	[FG_DATA_CYCLE_COUNT] = {
		.address	= 75,
		.offset		= 0,
		.length		= 2,
		.decode		= fg_decode_value_le
	},
	[FG_DATA_CURRENT] = {
		.address	= 0xa2,
		.length		= 2,
		.type		= BATT_BASE_PARAM,
		.wa_flags	= PMI8998_V2_REV_WA,
		.numrtr		= 1000,
		.denmtr		= 488281,
		.decode 	= fg_decode_current,
	},
	[FG_SETTING_CUTOFF_VOLT] = {
		.address	= 5,
		.offset		= 0,
		.length		= 2,
		.numrtr		= PMI8998_V1_LSB_15B_NUMRTR * 1000,
		.denmtr		= PMI8998_V1_LSB_15B_DENMTR,
		.encode		= fg_encode_voltage,
	},
	[FG_SETTING_EMPTY_VOLT] = {
		.address	= 15,
		.offset		= 3,
		.length		= 1,
		.numrtr		= PMI8998_V1_LSB_16B_NUMRTR * 1000,
		.denmtr		= PMI8998_V1_LSB_16B_DENMTR,
		.val_offset	= -2500,
		.encode		= fg_encode_voltage,
	},
	[FG_SETTING_MAX_VOLT] = {
		.address	= 16,
		.offset		= 2,
		.length		= 1,
		.numrtr		= 1000,
		.denmtr		= 15625,
		.val_offset	= -2000,
		.encode		= fg_encode_voltage,
	},
	[FG_SETTING_BATT_LOW] = {
		.address	= 16,
		.offset		= 1,
		.length		= 1,
		.numrtr		= PMI8998_V1_LSB_16B_NUMRTR * 1000,
		.denmtr		= PMI8998_V1_LSB_16B_DENMTR,
		.val_offset	= -2500,
		.encode		= fg_encode_voltage,
	},
	[FG_SETTING_CONST_CHARGE_VOLT_THR] = {
		.address	= 7,
		.offset		= 0,
		.length		= 2,
		.numrtr		= PMI8998_V1_LSB_15B_NUMRTR,
		.denmtr		= PMI8998_V1_LSB_15B_DENMTR,
		.encode		= fg_encode_voltage,
	},
	[FG_SETTING_TERM_CURRENT] = {
		.address	= 4,
		.offset		= 0,
		.length		= 3,
		.numrtr		= 1000000,
		.denmtr		= 122070,
		.encode		= fg_encode_current,
	},
	[FG_SETTING_SYS_TERM_CURRENT] = {
		.address	= 6,
		.offset		= 0,
		.length		= 3,
		.numrtr		= 100000,
		.denmtr		= 390625,
		.encode		= fg_encode_current,
	},
	[FG_SETTING_CHG_TERM_CURRENT] = {
		.address	= 14,
		.offset		= 1,
		.length		= 1,
		.numrtr		= PMI8998_V1_LSB_16B_NUMRTR * 1000,
		.denmtr		= PMI8998_V1_LSB_16B_DENMTR,
		.encode		= fg_encode_current,
	},
	[FG_SETTING_CHG_TERM_BASE_CURRENT] = {
		.address	= 15,
		.offset		= 0,
		.length		= 1,
		.numrtr		= 1024,
		.denmtr		= 1000,
		.encode		= fg_encode_current,
	},
	[FG_SETTING_DELTA_BSOC] = {
		.address	= 13,
		.offset		= 2,
		.length		= 1,
		.numrtr		= 2048,
		.denmtr		= 100,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_DELTA_MSOC] = {
		.address	= 12,
		.offset		= 3,
		.length		= 1,
		.numrtr		= 2048,
		.denmtr		= 100,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_RECHARGE_THR] = {
		.address	= 14,
		.offset		= 0,
		.length		= 1,
		.numrtr		= 256,
		.denmtr		= 100,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_RECHARGE_VOLT_THR] = {
		.address	= 16,
		.offset		= 1,
		.length		= 1,
		.numrtr		= 1000,
		.denmtr		= 15625,
		.val_offset	= -2000,
		.encode		= fg_encode_voltage,
	},
	[FG_SETTING_RSLOW_DISCHG] = {
		.address	= 34,
		.offset		= 0,
		.length		= 1,
	},
	[FG_SETTING_RSLOW_CHG] = {
		.address	= 51,
		.offset		= 0,
		.length		= 1,
	},
	[FG_SETTING_ESR_TIMER_DISCHG_MAX] = {
		.address	= 17,
		.offset		= 0,
		.length		= 2,
		.numrtr		= 1,
		.denmtr		= 1,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_ESR_TIMER_DISCHG_INIT] = {
		.address	= 17,
		.offset		= 2,
		.length		= 2,
		.numrtr		= 1,
		.denmtr		= 1,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_ESR_TIMER_CHG_MAX] = {
		.address	= 18,
		.offset		= 0,
		.length		= 2,
		.numrtr		= 1,
		.denmtr		= 1,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_ESR_TIMER_CHG_INIT] = {
		.address	= 18,
		.offset		= 2,
		.length		= 2,
		.numrtr		= 1,
		.denmtr		= 1,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_ESR_PULSE_THR] = {
		.address	= 2,
		.offset		= 3,
		.length		= 1,
		.numrtr		= 100000,
		.denmtr		= 390625,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_ESR_TIGHT_FILTER] = {
		.address	= 8,
		.offset		= 0,
		.length		= 1,
		.numrtr		= 512,
		.denmtr		= 1000000,
		.encode		= fg_encode_default
	},
	[FG_SETTING_ESR_BROAD_FILTER] = {
		.address	= 8,
		.offset		= 1,
		.length		= 1,
		.numrtr		= 512,
		.denmtr		= 1000000,
		.encode		= fg_encode_default
	},
	[FG_SETTING_KI_COEFF_LOW_DISCHG] = {
		.address	= 9,
		.offset		= 3,
		.length		= 1,
		.numrtr		= 1000,
		.denmtr		= 244141,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_KI_COEFF_MED_DISCHG] = {
		.address	= 10,
		.length		= 1,
		.offset		= 0,
		.numrtr		= 1000,
		.denmtr		= 244141,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_KI_COEFF_HI_DISCHG] = {
		.address	= 10,
		.length		= 1,
		.offset		= 1,
		.numrtr		= 1000,
		.denmtr		= 244141,
		.encode		= fg_encode_default,
	},
	[FG_SETTING_KI_COEFF_FULL_SOC] = {
		.address	= 12,
		.length		= 1,
		.offset		= 2,
		.numrtr		= 1000,
		.denmtr		= 244141,
		.encode		= fg_encode_default,
	},
	[FG_PARAM_MONOTONIC_SOC] = {
		.address	= 94,
		.offset		= 2,
		.length		= 2,
	},
	[FG_PROFILE_INTEGRITY] = {
		.address	= 79,
		.offset		= 1,
		.length		= 3,
	},
};

struct fg_capacity_learning_data {
	struct mutex	lock;
	bool		active;
	int		init_cc_uah;
	int		nom_cap_uah;
	int		init_cc_soc_sw;
	int		final_cc_uah;
	int		learned_cc_uah;
	/*params*/
	int		max_start_soc;
	int		max_cap_inc;
	int		max_cap_dec;
	int		vbat_est_thr_uv;
	int		max_cap_limit;
	int		min_cap_limit;
	int		min_temp;
	int		max_temp;
};

struct fg_rslow_data {
	u8			rslow_cfg;
	u8			rslow_thr;
	u8			rs_to_rslow[2];
	u8			rslow_comp[4];
	uint32_t		chg_rs_to_rslow;
	uint32_t		chg_rslow_comp_c1;
	uint32_t		chg_rslow_comp_c2;
	uint32_t		chg_rslow_comp_thr;
	bool			active;
	struct mutex		lock;
};

#define BUCKET_COUNT			8
#define BUCKET_SOC_PCT			(256 / BUCKET_COUNT)
struct fg_cyc_ctr_data {
	bool			en;
	bool			started[BUCKET_COUNT];
	u16			count[BUCKET_COUNT];
	u8			last_soc[BUCKET_COUNT];
	int			id;
	struct mutex		lock;
};

struct battery_info {
	const char *manufacturer;
	const char *model;
	const char *serial_num;

	bool cyc_ctr_en;
	bool nom_cap_unbound;

	u8 thermal_coeffs[6];
	u8 thermal_coeffs_len;

	u8 *batt_profile;
	unsigned batt_profile_len;

	int rconn_mohm;
	int nom_cap_uah;
	int batt_max_voltage_uv;
	int fastchg_curr_ma;
};

struct fg_irq {
	const char *name;
	irqreturn_t (*handler)(int, void *);
	int irq_mask;
	int irq;
	bool wakeable;
};

struct fg_iadc_comp_data {
	u8			dfl_gain_reg[2];
	bool			gain_active;
	int64_t			dfl_gain;
};

enum esr_filter_status {
	ROOM_TEMP = 1,
	LOW_TEMP,
	RELAX_TEMP,
};

enum esr_timer_config {
	TIMER_RETRY = 0,
	TIMER_MAX,
	NUM_ESR_TIMERS,
};

#define KI_COEFF_MAX			62200
#define KI_COEFF_SOC_LEVELS		3
struct fg_dt_props {
	int term_current_ma;
	int chg_term_current_ma;
	int chg_term_base_current_ma;
	int sys_term_current_ma;
	int cutoff_volt_mv;
	int recharge_thr;
	int recharge_volt_thr_mv;
	int const_charge_volt_mv;
	int cool_temp;
	int warm_temp;
	int hot_temp;
	int cold_temp;
	int empty_irq_volt_mv;
	int low_volt_thr_mv;
	int bcl_lm_ma;
	int bcl_mh_ma;
	int esr_timer_charging[NUM_ESR_TIMERS];
	int esr_timer_awake[NUM_ESR_TIMERS];
	int esr_timer_asleep[NUM_ESR_TIMERS];
	int esr_timer_shutdown[NUM_ESR_TIMERS];
	int esr_tight_flt_upct;
	int esr_broad_flt_upct;
	int esr_tight_lt_flt_upct;
	int esr_broad_lt_flt_upct;
	int esr_tight_rt_flt_upct;
	int esr_broad_rt_flt_upct;
	int esr_flt_switch_temp;
	int esr_pulse_thresh_ma;
	int esr_meas_curr_ma;
	int esr_clamp_mohms;
	int vbatt_est_diff;
	int ki_coeff_full_soc_dischg;
	int ki_coeff_hi_chg;
	int ki_coeff_soc[KI_COEFF_SOC_LEVELS];
	int ki_coeff_low_dischg[KI_COEFF_SOC_LEVELS];
	int ki_coeff_med_dischg[KI_COEFF_SOC_LEVELS];
	int ki_coeff_hi_dischg[KI_COEFF_SOC_LEVELS];
};

enum temperature {
	BATT_WARM,
	BATT_COOL,
	BATT_HOT,
	BATT_COLD
};

struct fg_chip {
	struct device *dev;
	struct regmap *regmap;
	struct mutex sram_rw_lock;

	struct power_supply *bms_psy;
	struct power_supply *batt_psy;
	struct power_supply *parallel_psy;
	struct power_supply *usb_psy;
	struct power_supply *dc_psy;

	u8 revision[4];
	enum pmic pmic_version;
	bool ima_supported;
	bool reset_on_lockup;

	/* base addresses of components*/
	unsigned int soc_base;
	unsigned int batt_base;
	unsigned int mem_base;
	u16 *offset;

	struct fg_sram_param *param;
	struct fg_irq *irqs;

	struct fg_cyc_ctr_data cyc_ctr;
	struct battery_info batt_info;
	struct fg_dt_props dt;
	struct fg_iadc_comp_data iadc_comp_data;

	struct fg_capacity_learning_data cl;
	struct fg_rslow_data rslow_comp;

	int last_cap;
	int health;
	int status;
	int prev_status;
	int charge_status;
	int prev_charge_status;
	enum temperature temp_status;
	int esr_timer_charging_default[NUM_ESR_TIMERS];
	int vbatt_est_diff;
	int batt_temp;
	int ki_coeff_full_soc;
	int delta_temp_irq_count;
	int esr_flt_sts;

	struct work_struct status_change_work;
	struct delayed_work profile_load_work;

	bool irqs_enabled;
	bool full_soc_irq_enabled;
	bool vbat_low_irq_enabled;
	bool use_vbat_low_empty_soc;
	bool power_supply_registered;
	bool charge_done;
	bool charge_full;
	bool esr_fcc_ctrl_en;
	bool ki_coeff_dischg_en;
	bool batt_missing;
	bool input_present;
	bool otg_present;
	bool first_profile_loaded;
	bool fg_restarting;
	bool soc_reporting_ready;

	struct alarm hard_jeita_alarm;
	struct alarm esr_filter_alarm;

	ktime_t last_delta_temp_time;

	struct completion first_soc_done;
	struct completion sram_access_granted;
	struct completion sram_access_revoked;

	int (*do_restart)(struct fg_chip *chip, bool write_profile);
	int (*rconn_config)(struct fg_chip *chip);
};

static bool fg_check_sram_access(struct fg_chip *chip);
static void fg_cap_learning_post_process(struct fg_chip *chip);
static void fg_iadc_gain_comp(struct fg_chip *chip);
static int fg_charge_full_update(struct fg_chip *chip);
static void fg_rslow_update(struct fg_chip *chip);
static void fg_update_esr_values(struct fg_chip *chip);
static void fg_cycle_counter_update(struct fg_chip *chip);

#define REASONABLY_BIG_TIMEOUT_MS	5000
static irqreturn_t fg_soc_irq_handler(int irq, void *_chip)
{
	struct fg_chip *chip = _chip;
	int soc_rt_sts;
	int rc;

	rc = regmap_read(chip->regmap, INT_RT_STS(chip->soc_base), &soc_rt_sts);
	if (rc)
		dev_err(chip->dev, "failed to get soc_int_sts: %d\n", rc);

	if (!chip->power_supply_registered)
		return IRQ_HANDLED;

	power_supply_changed(chip->bms_psy);

	if (chip->rslow_comp.chg_rs_to_rslow > 0 &&
			chip->rslow_comp.chg_rslow_comp_c1 > 0 &&
			chip->rslow_comp.chg_rslow_comp_c2 > 0)
		fg_rslow_update(chip);

	if (chip->batt_info.cyc_ctr_en)
		fg_cycle_counter_update(chip);

	fg_update_esr_values(chip);

	rc = fg_charge_full_update(chip);
	if (rc)
		dev_err(chip->dev, "charge_full_update failed: %d\n", rc);

	if (chip->iadc_comp_data.gain_active)
		fg_iadc_gain_comp(chip);

	return IRQ_HANDLED;
}

#define SOC_EMPTY	BIT(3)
static irqreturn_t fg_empty_soc_irq_handler(int irq, void *_chip)
{
	struct fg_chip *chip = _chip;
	int soc_rt_sts;
	int rc;

	rc = regmap_read(chip->regmap, INT_RT_STS(chip->soc_base), &soc_rt_sts);
	if (rc) {
		dev_err(chip->dev, "spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->soc_base), rc);
		return IRQ_HANDLED;
	}

	if (soc_rt_sts & SOC_EMPTY && chip->power_supply_registered)
		power_supply_changed(chip->bms_psy);

	return IRQ_HANDLED;
}

static irqreturn_t fg_first_soc_irq_handler(int irq, void *_chip)
{
	struct fg_chip *chip = _chip;

	if (chip->power_supply_registered)
		power_supply_changed(chip->bms_psy);

	complete_all(&chip->first_soc_done);

	return IRQ_HANDLED;
}

#define BATT_SOFT_COLD_STS	BIT(0)
#define BATT_SOFT_HOT_STS	BIT(1)
#define HARD_JEITA_ALARM_CHECK_NS	10000000000
static irqreturn_t fg_jeita_soft_hot_irq_handler(int irq, void *_chip)
{
	int rc;
	struct fg_chip *chip = _chip;
	int regval;
	bool batt_warm;

	if (chip->temp_status == BATT_WARM)
		return IRQ_HANDLED;

	rc = regmap_read(chip->regmap, INT_RT_STS(chip->batt_base), &regval);
	if (rc) {
		dev_err(chip->dev, "spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->batt_base), rc);
		return IRQ_HANDLED;
	}

	batt_warm = !!(regval & BATT_SOFT_HOT_STS);
	if (batt_warm) {
		alarm_start_relative(&chip->hard_jeita_alarm,
				ns_to_ktime(HARD_JEITA_ALARM_CHECK_NS));
	} else {
		alarm_try_to_cancel(&chip->hard_jeita_alarm);
	}

	return IRQ_HANDLED;
}

static irqreturn_t fg_jeita_soft_cold_irq_handler(int irq, void *_chip)
{
	int rc;
	struct fg_chip *chip = _chip;
	int regval;
	bool batt_cool;

	rc = regmap_read(chip->regmap, INT_RT_STS(chip->batt_base), &regval);
	if (rc) {
		dev_err(chip->dev, "spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->batt_base), rc);
		return IRQ_HANDLED;
	}

	batt_cool = !!(regval & BATT_SOFT_COLD_STS);
	if (batt_cool) {
		alarm_start_relative(&chip->hard_jeita_alarm,
				ns_to_ktime(HARD_JEITA_ALARM_CHECK_NS));
	} else {
		alarm_try_to_cancel(&chip->hard_jeita_alarm);
	}

	return IRQ_HANDLED;
}

#define VBATT_LOW_STS_BIT BIT(2)
static int fg_get_vbatt_status(struct fg_chip *chip, bool *vbatt_low_sts)
{
	int rc = 0;
	int fg_batt_sts;

	rc = regmap_read(chip->regmap, INT_RT_STS(chip->batt_base),
			&fg_batt_sts);
	if (rc)
		dev_err(chip->dev, "failed to get batt int_rt_sts: %d\n", rc);
	else
		*vbatt_low_sts = !!(fg_batt_sts & VBATT_LOW_STS_BIT);

	return rc;
}

static irqreturn_t fg_vbatt_low_handler(int irq, void *_chip)
{
	struct fg_chip *chip = _chip;
	bool vbatt_low_sts;

	if (chip->status == POWER_SUPPLY_STATUS_CHARGING) {
		if (fg_get_vbatt_status(chip, &vbatt_low_sts))
			goto out;
		if (!vbatt_low_sts && chip->vbat_low_irq_enabled) {
			chip->vbat_low_irq_enabled = false;
		}
	}
	if (chip->power_supply_registered)
		power_supply_changed(chip->bms_psy);
out:

	return IRQ_HANDLED;
}

static irqreturn_t fg_mem_avail_irq_handler(int irq, void *_chip)
{
	struct fg_chip *chip = _chip;
	int mem_if_sts;
	int rc;

	rc = regmap_read(chip->regmap, INT_RT_STS(chip->mem_base), &mem_if_sts);
	if (rc) {
		dev_err(chip->dev, "failed to read mem status rc=%d\n", rc);
		return IRQ_HANDLED;
	}

	if (fg_check_sram_access(chip)) {
		reinit_completion(&chip->sram_access_revoked);
		complete_all(&chip->sram_access_granted);
	} else {
		complete_all(&chip->sram_access_revoked);
	}

	return IRQ_HANDLED;
}

#define MEM_XCP_BIT				BIT(1)
int fg_check_and_clear_dma_errors(struct fg_chip *chip);
static int fg_check_and_clear_ima_errors(struct fg_chip *chip,
		bool check_hw_sts);
static irqreturn_t fg_mem_xcp_irq_handler(int irq, void *data)
{
	struct fg_chip *chip = data;
	int status;
	int rc;

	rc = regmap_read(chip->regmap, INT_RT_STS(chip->mem_base), &status);
	if (rc < 0) {
		dev_err(chip->dev, "failed to read mem_int_sts: %d\n", rc);
		return IRQ_HANDLED;
	}

	mutex_lock(&chip->sram_rw_lock);
	rc = fg_check_and_clear_dma_errors(chip);
	if (rc < 0)
		pr_err("Error in clearing DMA error, rc=%d\n", rc);

	if (status & MEM_XCP_BIT) {
		rc = fg_check_and_clear_ima_errors(chip, true);
		if (rc < 0 && rc != -EAGAIN)
			dev_err(chip->dev, "failed to clear IMA errors: %d\n",
					rc);
	}

	mutex_unlock(&chip->sram_rw_lock);
	return IRQ_HANDLED;
}

static void fg_cap_learning_update(struct fg_chip*);
static int fg_esr_validate(struct fg_chip *chip);
static int fg_get_temperature(struct fg_chip *chip, int *val);
static int fg_esr_filter_config(struct fg_chip *chip, int batt_temp,
				bool override);
static int fg_adjust_ki_coeff_full_soc(struct fg_chip *chip, int batt_temp);

static void fg_notify_charger(struct fg_chip *chip)
{
	union power_supply_propval prop = {0, };
	int rc;

	if (!chip->batt_psy)
		return;

	prop.intval = chip->dt.const_charge_volt_mv;
	rc = power_supply_set_property(chip->batt_psy,
			POWER_SUPPLY_PROP_VOLTAGE_MAX, &prop);
	if (rc < 0) {
		pr_err("Error in setting voltage_max property on batt_psy, rc=%d\n",
			rc);
		return;
	}

	prop.intval = chip->batt_info.fastchg_curr_ma * 1000;
	rc = power_supply_set_property(chip->batt_psy,
			POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT, &prop);
	if (rc < 0) {
		pr_err("Error in setting constant_charge_current_max property on batt_psy, rc=%d\n",
			rc);
		return;
	}
}

static bool batt_psy_initialized(struct fg_chip *chip)
{
	if (chip->batt_psy)
		return true;

	chip->batt_psy = power_supply_get_by_name("battery");
	if (!chip->batt_psy)
		return false;

	/* batt_psy is initialized, set the fcc and fv */
	fg_notify_charger(chip);

	return true;
}

static int fg_adjust_ki_coeff_dischg(struct fg_chip *chip);

static irqreturn_t fg_delta_msoc_irq_handler(int irq, void *data)
{
	struct fg_chip *chip = data;
	int rc;

	fg_cycle_counter_update(chip);

	if (chip->cl.active)
		fg_cap_learning_update(chip);

	rc = fg_charge_full_update(chip);
	if (rc)
		dev_err(chip->dev, "failed to set charge_full_update: %d\n", rc);

	rc = fg_adjust_ki_coeff_dischg(chip);
	if (rc)
		dev_err(chip->dev, "failed to set ki_coeff_dischg: %d\n", rc);

	rc = fg_esr_validate(chip);
	if (rc < 0)
		dev_err(chip->dev, "failed to validate ESR: %d\n", rc);

	if (batt_psy_initialized(chip))
		power_supply_changed(chip->batt_psy);

	return IRQ_HANDLED;
}

static irqreturn_t fg_delta_bsoc_irq_handler(int irq, void *data)
{
	struct fg_chip *chip = data;
	int rc;

	rc = fg_charge_full_update(chip);
	if (rc < 0)
		dev_err(chip->dev, "charge_full_update failed: %d\n", rc);

	return IRQ_HANDLED;
}

static irqreturn_t fg_delta_batt_temp_irq_handler(int irq, void *data)
{
	struct fg_chip *chip = data;
	union power_supply_propval prop = {0, };
	int rc, batt_temp;

	rc = fg_get_temperature(chip, &batt_temp);
	if (rc < 0) {
		pr_err("Error in getting batt_temp\n");
		return IRQ_HANDLED;
	}

	rc = fg_esr_filter_config(chip, batt_temp, false);
	if (rc < 0)
		dev_err(chip->dev, "failed to config ESR filter: %d\n", rc);

	rc = fg_adjust_ki_coeff_full_soc(chip, batt_temp);
	if (rc < 0)
		dev_err(chip->dev, "failed to set ki_coeff_full_soc: %d\n", rc);

	if (!batt_psy_initialized(chip)) {
		chip->batt_temp = batt_temp;
		return IRQ_HANDLED;
	}

	power_supply_get_property(chip->batt_psy, POWER_SUPPLY_PROP_HEALTH,
		&prop);
	chip->health = prop.intval;

	if (chip->batt_temp != batt_temp) {
		chip->batt_temp = batt_temp;
		power_supply_changed(chip->batt_psy);
	}

	return IRQ_HANDLED;
}

static void clear_cycle_counter(struct fg_chip *chip);

#define BATT_MISSING_STS BIT(6)
static irqreturn_t fg_batt_missing_irq_handler(int irq, void *_chip)
{
	struct fg_chip *chip = _chip;
	int status, rc;

	rc = regmap_read(chip->regmap, INT_RT_STS(chip->batt_base), &status);
	if (rc) {
		dev_err(chip->dev, "failed to read batt int sts: %d\n", rc);
		return IRQ_HANDLED;
	}
	chip->batt_missing = !!(status & BATT_MISSING_STS);

	if (chip->batt_missing) {
		mutex_lock(&chip->cyc_ctr.lock);
		clear_cycle_counter(chip);
		mutex_unlock(&chip->cyc_ctr.lock);
		chip->soc_reporting_ready = false;
	} else {
		//TODO:schedule_delayed_work(&chip->profile_load_work, 0);
	}

	if (chip->power_supply_registered)
		power_supply_changed(chip->bms_psy);
	return IRQ_HANDLED;
}

enum fg_irq_ids_pmi8950 {
	FULL_SOC_IRQ,
	EMPTY_SOC_IRQ,
	DELTA_SOC_IRQ,
	FIRST_EST_DONE,
	SOFT_COLD_IRQ,
	SOFT_HOT_IRQ,
	VBATT_LOW_8950_IRQ,
	BATT_MISSING_8950_IRQ,
	MEM_AVAIL_IRQ,
	FG_IRQS_MAX_PMI8950
};

struct fg_irq fg_irqs_pmi8950[] = {
	[FULL_SOC_IRQ] = {
		.name	 	= "full-soc",
		.handler	= fg_soc_irq_handler,
		.irq_mask	= IRQF_TRIGGER_RISING
	},
	[EMPTY_SOC_IRQ] = {
		.name		= "empty-soc",
		.handler	= fg_empty_soc_irq_handler,
		.irq_mask	= IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
	},
	[DELTA_SOC_IRQ] = {
		.name		= "delta-soc",
		.handler	= fg_soc_irq_handler,
		.irq_mask	= IRQF_TRIGGER_RISING
	},
	[FIRST_EST_DONE] = {
		.name		= "first-est-done",
		.handler	= fg_first_soc_irq_handler,
		.irq_mask	= IRQF_TRIGGER_RISING
	},
	[SOFT_COLD_IRQ] = {
		.name		= "soft-cold",
		.handler	= fg_jeita_soft_cold_irq_handler,
		.irq_mask	= IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
			| IRQF_ONESHOT
	},
	[SOFT_HOT_IRQ] = {
		.name		= "soft-hot",
		.handler	= fg_jeita_soft_hot_irq_handler,
		.irq_mask	= IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
			| IRQF_ONESHOT
	},
	[VBATT_LOW_8950_IRQ] = {
		.name		= "vbatt-low",
		.handler	= fg_vbatt_low_handler,
		.irq_mask	= IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
	},
	[BATT_MISSING_8950_IRQ] = {
		.name		= "batt-missing",
		.handler	= fg_batt_missing_irq_handler,
		.irq_mask	= IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
			| IRQF_ONESHOT
	},
	[MEM_AVAIL_IRQ] = {
		.name		= "mem-avail",
		.handler	= fg_mem_avail_irq_handler,
		.irq_mask 	= IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
	},
};

enum fg_irq_ids_pmi8998 {
	MSOC_EMPTY_IRQ,
	MSOC_DELTA_IRQ,
	BSOC_DELTA_IRQ,
	SOC_READY_IRQ,
	BATT_TEMP_DELTA_IRQ,
	VBATT_LOW_8998_IRQ,
	MEM_XCP_IRQ,
	FG_IRQS_MAX_PMI8998
};

struct fg_irq fg_irqs_pmi8998[] = {
	[MSOC_EMPTY_IRQ] = {
		.name		= "msoc-empty",
		.handler	= fg_empty_soc_irq_handler,
		.wakeable	= true,
	},
	[MSOC_DELTA_IRQ] = {
		.name		= "msoc-delta",
		.handler	= fg_delta_msoc_irq_handler,
		.wakeable	= true,
	},
	[BSOC_DELTA_IRQ] = {
		.name		= "bsoc-delta",
		.handler	= fg_delta_bsoc_irq_handler,
		.wakeable	= true,
	},
	[SOC_READY_IRQ] = {
		.name		= "soc-ready",
		.handler	= fg_first_soc_irq_handler,
		.wakeable	= true,
	},
	[BATT_TEMP_DELTA_IRQ] = {
		.name		= "batt-temp-delta",
		.handler	= fg_delta_batt_temp_irq_handler,
		.wakeable	= true,
	},
	[VBATT_LOW_8998_IRQ] = {
		.name		= "vbatt-low",
		.handler	= fg_vbatt_low_handler,
		.wakeable	= true,
	},
	[MEM_XCP_IRQ] = {
		.name		= "mem-xcp",
		.handler	= fg_mem_xcp_irq_handler,
	},
};

static int fg_init_irqs_pmi8950(struct fg_chip *chip)
{
	int i, rc;

	for (i = 0; i < FG_IRQS_MAX_PMI8950; ++i) {
		fg_irqs_pmi8950[i].irq = of_irq_get_byname(chip->dev->of_node,
				fg_irqs_pmi8950[i].name);
		if (fg_irqs_pmi8950[i].irq < 0) {
			dev_err(chip->dev, "failed to get irq %s: %d\n",
					fg_irqs_pmi8950[i].name,
					fg_irqs_pmi8950[i].irq);
			return fg_irqs_pmi8950[i].irq;
		}

		if (i == SOFT_COLD_IRQ || i == SOFT_HOT_IRQ
				|| i == BATT_MISSING_8950_IRQ) {
			rc = devm_request_threaded_irq(chip->dev,
					fg_irqs_pmi8950[i].irq, NULL,
					fg_irqs_pmi8950[i].handler,
					fg_irqs_pmi8950[i].irq_mask,
					fg_irqs_pmi8950[i].name, chip);
			if (rc) {
				dev_err(chip->dev, "failed to get irq %s: %d\n",
						fg_irqs_pmi8950[i].name, rc);
				return rc;
			}
		} else {
			rc = devm_request_irq(chip->dev,
					fg_irqs_pmi8950[i].irq,
					fg_irqs_pmi8950[i].handler,
					fg_irqs_pmi8950[i].irq_mask,
					fg_irqs_pmi8950[i].name, chip);
			if (rc) {
				dev_err(chip->dev, "failed to get irq %s: %d\n",
						fg_irqs_pmi8950[i].name, rc);
				return rc;
			}
		}
	}

	return 0;
}

static int fg_init_irqs_pmi8998(struct fg_chip *chip)
{
	int i, rc;

	for (i = 0; i < FG_IRQS_MAX_PMI8998; ++i) {
		fg_irqs_pmi8950[i].irq = of_irq_get_byname(chip->dev->of_node,
				fg_irqs_pmi8950[i].name);
		if (fg_irqs_pmi8950[i].irq < 0) {
			dev_err(chip->dev, "failed to get irq %s: %d\n",
					fg_irqs_pmi8950[i].name,
					fg_irqs_pmi8998[i].irq);
			return fg_irqs_pmi8998[i].irq;
		}

		rc = devm_request_threaded_irq(chip->dev,
				fg_irqs_pmi8998[i].irq, NULL,
				fg_irqs_pmi8998[i].handler, IRQF_ONESHOT,
				fg_irqs_pmi8998[i].name, chip);
		if (rc) {
			dev_err(chip->dev, "failed to get irq %s: %d\n",
					fg_irqs_pmi8998[i].name, rc);
			return rc;
		}
	}

	return 0;
}

struct fg_pmic_data {
	enum pmic pmic_version;
	struct fg_sram_param *params;
	struct fg_irq *irqs;
	int (*init_irqs)(struct fg_chip *chip);
	void (*status_change_work)(struct work_struct * work);
	int (*do_restart)(struct fg_chip *chip, bool write_profile);
	int (*rconn_config)(struct fg_chip *chip);
};

/* All getters HERE */

#define VOLTAGE_15BIT_MASK	GENMASK(14, 0)
static int fg_decode_voltage_15b(struct fg_sram_param sp, u8 *value)
{
	int temp = *(int *) value;
	temp &= VOLTAGE_15BIT_MASK;
	return div_u64((u64)temp * sp.denmtr, sp.numrtr);
}

static int fg_decode_cc_soc_pmi8998(struct fg_sram_param sp, u8 *value)
{
	int temp = *(int *) value;
	temp = div_s64((s64)temp * sp.denmtr, sp.numrtr);
	temp = sign_extend32(temp, 31);
	return temp;
}

#define CC_SOC_MAGNITUDE_MASK	0x1FFFFFFF
#define CC_SOC_NEGATIVE_BIT	BIT(29)
#define FULL_PERCENT_28BIT	0xFFFFFFF
#define FULL_RESOLUTION		1000000
static int fg_decode_cc_soc_pmi8950(struct fg_sram_param sp, u8 *val)
{
	int64_t cc_pc_val, cc_soc_pc;
	int temp, magnitude;

	temp = val[3] << 24 | val[2] << 16 | val[1] << 8 | val[0];
	magnitude = temp & CC_SOC_MAGNITUDE_MASK;

	if (temp & CC_SOC_NEGATIVE_BIT)
		cc_pc_val = -1 * (~magnitude + 1);
	else
		cc_pc_val = magnitude;

	cc_soc_pc = div64_s64(cc_pc_val * 100 * FULL_RESOLUTION,
			      FULL_PERCENT_28BIT);
	return cc_soc_pc;
}

static int64_t twos_compliment_extend(int64_t val, int nbytes)
{
	int i;
	int64_t mask;

	mask = 0x80LL << ((nbytes - 1) * 8);
	if (val & mask) {
		for (i = 8; i > nbytes; i--) {
			mask = 0xFFLL << ((i - 1) * 8);
			val |= mask;
		}
	}

	return val;
}

static int fg_decode_current(struct fg_sram_param sp, u8 *val)
{
	int64_t temp;
	if (sp.wa_flags & PMI8998_V1_REV_WA)
		temp = val[0] << 8 | val[1];
	else
		temp = val[1] << 8 | val[0];
	temp = twos_compliment_extend(temp, 2);
	return div_s64((s64)temp * sp.denmtr,
			sp.numrtr);
}

static int fg_decode_value_16b(struct fg_sram_param sp, u8 *value)
{
	int temp = 0, i;
	if (sp.wa_flags & PMI8998_V2_REV_WA)
		for (i = 0; i < sp.length; ++i)
			temp |= value[i] << (8 * (sp.length - i));
	else
		for (i = 0; i < sp.length; ++i)
			temp |= value[i] << (8 * i);
	return div_u64((u64)(u16)temp * sp.denmtr, sp.numrtr) + sp.val_offset;
}

#define BATT_TEMP_LSB_MASK			GENMASK(7, 0)
#define BATT_TEMP_MSB_MASK			GENMASK(2, 0)
static int fg_decode_temperature(struct fg_sram_param sp, u8 *val)
{
	int temp;

	temp = ((val[1] & BATT_TEMP_MSB_MASK) << 8) |
		(val[0] & BATT_TEMP_LSB_MASK);
	temp = DIV_ROUND_CLOSEST(temp * sp.denmtr, sp.numrtr);

	return temp + sp.val_offset;
}

#define EXPONENT_MASK		0xF800
#define MANTISSA_MASK		0x3FF
#define SIGN			BIT(10)
#define EXPONENT_SHIFT		11
static int fg_decode_float(struct fg_sram_param sp, u8 *raw_val)
{
	int64_t final_val, exponent_val, mantissa_val;
	int exponent, mantissa, n, i;
	bool sign;
	int value = 0;

	for (i = 0; i < sp.length; ++i)
		value |= raw_val[i] << (8 * i);

	exponent = (value & EXPONENT_MASK) >> EXPONENT_SHIFT;
	mantissa = (value & MANTISSA_MASK);
	sign = !!(value & SIGN);

	mantissa_val = mantissa * MICRO_UNIT;

	n = exponent - 15;
	if (n < 0)
		exponent_val = MICRO_UNIT >> -n;
	else
		exponent_val = MICRO_UNIT << n;

	n = n - 10;
	if (n < 0)
		mantissa_val >>= -n;
	else
		mantissa_val <<= n;

	final_val = exponent_val + mantissa_val;

	if (sign)
		final_val *= -1;

	return final_val;
}

static int fg_decode_value_le(struct fg_sram_param sp, u8 *value)
{
	int temp = value[1] << 8 | value[0];
	if (sp.numrtr)
		return temp * sp.numrtr;
	return temp;
}

static int fg_decode_default(struct fg_sram_param sp, u8 *value)
{
	int temp = (0xffffffff >> (8 * (4 - sp.length))) & *(int *) value;
	if (sp.numrtr)
		return temp * sp.numrtr;
	return temp;
}

static void fg_encode_voltage(struct fg_sram_param sp, int val_mv, u8 *buf)
{
	int i, mask = 0xff;
	int64_t temp;

	val_mv += sp.val_offset;
	temp = (int64_t)div_u64((u64)val_mv * sp.numrtr, sp.denmtr);
	for (i = 0; i < sp.length; i++) {
		buf[i] = temp & mask;
		temp >>= 8;
	}
}

static void fg_encode_current(struct fg_sram_param sp, int val_ma, u8 *buf)
{
	int i, mask = 0xff;
	int64_t temp;
	s64 current_ma;

	current_ma = val_ma;
	temp = (int64_t)div_s64(current_ma * sp.numrtr, sp.denmtr);
	for (i = 0; i < sp.length; i++) {
		buf[i] = temp & mask;
		temp >>= 8;
	}
}

static void fg_encode_roundoff(struct fg_sram_param sp, int val, u8 *buf)
{
	int i, mask = 0xff;

	val = DIV_ROUND_CLOSEST(val * sp.numrtr, sp.denmtr);
	for (i = 0; i < sp.length; ++i)
		buf[i] = val & (mask >> (8 * i));
}

static void fg_encode_voltcmp8(struct fg_sram_param sp, int val, u8 *buf)
{
	buf[0] = (u8)((val - 2500000) / 9766);
}

static void fg_encode_default(struct fg_sram_param sp, int val, u8 *buf)
{
	int i, mask = 0xff;
	int64_t temp;

	temp = (int64_t)div_s64((s64)val * sp.numrtr, sp.denmtr);
	for (i = 0; i < sp.length; i++) {
		buf[i] = temp & mask;
		temp >>= 8;
	}
}

#define MIN_HALFFLOAT_EXP_N		-15
#define MAX_HALFFLOAT_EXP_N		 16
static int log2_floor(int64_t uval)
{
	int n = 0;
	int64_t i = MICRO_UNIT;

	if (uval > i) {
		while (uval > i && n > MIN_HALFFLOAT_EXP_N) {
			i <<= 1;
			n += 1;
		}
		if (uval < i)
			n -= 1;
	} else if (uval < i) {
		while (uval < i && n < MAX_HALFFLOAT_EXP_N) {
			i >>= 1;
			n -= 1;
		}
	}

	return n;
}

static int64_t exp2_int(int64_t n)
{
	int p = n - 1;

	if (p > 0)
		return (2 * MICRO_UNIT) << p;
	else
		return (2 * MICRO_UNIT) >> abs(p);
}

static void fg_encode_float(struct fg_sram_param sp, int val, u8 *buf)
{
	int sign = 0, n, exp, mantissa;
	u16 half = 0;

	if (val < 0) {
		sign = 1;
		val = abs(val);
	}
	n = log2_floor(val);
	exp = n + 15;
	mantissa = div_s64(div_s64((val - exp2_int(n)) * exp2_int(10 - n),
				MICRO_UNIT) + MICRO_UNIT / 2, MICRO_UNIT);

	half = (mantissa & MANTISSA_MASK) | ((sign << 10) & SIGN)
		| ((exp << 11) & EXPONENT_MASK);

	buf[0] = half >> 1;
	buf[1] = half & 0xff;
}

static void fg_encode_bcl(struct fg_sram_param sp, int val, u8 *buf)
{
	buf[0] = val * sp.numrtr / sp.denmtr;
}

static void fg_encode_adc(struct fg_sram_param sp, int val, u8 *buf)
{
	val = DIV_ROUND_CLOSEST(val * sp.numrtr, sp.denmtr);
	buf[0] = val & 0xff;
	buf[1] = val >> 8;
}

static int fg_write(struct fg_chip *chip, u8 *val, u16 addr, int len)
{
	int rc;
	bool sec_access = (addr & 0xff) > 0xd0;
	u8 sec_addr_val = 0xa5;

	if (sec_access) {
		pr_info("sec_access addr = %d\n", addr);
		rc = regmap_bulk_write(chip->regmap,
				(addr & 0xff00) | 0xd0,
				&sec_addr_val, 1);
	}

	if ((addr & 0xff00) == 0) {
		pr_err("addr cannot be zero base=0x%02x\n", addr);
		return -EINVAL;
	}

	return regmap_bulk_write(chip->regmap, addr, val, len);
}

static int fg_read(struct fg_chip *chip, u8 *val, u16 addr, int len)
{
	if ((addr & 0xff00) == 0) {
		pr_err("base cannot be zero base=0x%02x\n", addr);
		return -EINVAL;
	}

	return regmap_bulk_read(chip->regmap, addr, val, len);
}

static int fg_masked_write(struct fg_chip *chip, u16 addr,
		u8 mask, u8 val)
{
	int error;
	u8 reg;
	error = fg_read(chip, &reg, addr, 1);
	if (error)
		return error;

	reg &= ~mask;
	reg |= val & mask;

	error = fg_write(chip, &reg, addr, 1);
	return error;
}

#define IMA_IACS_CLR			BIT(2)
#define IMA_IACS_RDY			BIT(1)
static int fg_run_iacs_clear_sequence(struct fg_chip *chip)
{
	int rc = 0;
	u8 temp;

	/* clear the error */
	rc = fg_masked_write(chip, MEM_INTF_IMA_CFG(chip),
				IMA_IACS_CLR, IMA_IACS_CLR);
	if (rc) {
		pr_err("Error writing to IMA_CFG, rc=%d\n", rc);
		return rc;
	}

	temp = 0x4;
	rc = fg_write(chip, &temp, MEM_INTF_ADDR_LSB(chip) + 1, 1);
	if (rc) {
		pr_err("Error writing to MEM_INTF_ADDR_MSB, rc=%d\n", rc);
		return rc;
	}

	temp = 0x0;
	rc = fg_write(chip, &temp, MEM_INTF_WR_DATA0(chip) + 3, 1);
	if (rc) {
		pr_err("Error writing to WR_DATA3, rc=%d\n", rc);
		return rc;
	}

	rc = fg_read(chip, &temp, MEM_INTF_RD_DATA0(chip) + 3, 1);
	if (rc) {
		pr_err("Error writing to RD_DATA3, rc=%d\n", rc);
		return rc;
	}

	rc = fg_masked_write(chip, MEM_INTF_IMA_CFG(chip),
				IMA_IACS_CLR, 0);
	if (rc) {
		pr_err("Error writing to IMA_CFG, rc=%d\n", rc);
		return rc;
	}
	return rc;
}

#define IACS_ERR_BIT		BIT(0)
#define XCT_ERR_BIT		BIT(1)
#define DATA_RD_ERR_BIT		BIT(3)
#define DATA_WR_ERR_BIT		BIT(4)
#define ADDR_BURST_WRAP_BIT	BIT(5)
#define ADDR_RNG_ERR_BIT	BIT(6)
#define ADDR_SRC_ERR_BIT	BIT(7)
static int fg_check_and_clear_ima_errors(struct fg_chip *chip,
		bool check_hw_sts)
{
	int rc = 0, ret = 0;
	u8 err_sts = 0, exp_sts = 0, hw_sts = 0;
	bool run_err_clr_seq = false;

	rc = fg_read(chip, &err_sts,
			MEM_INTF_IMA_ERR_STS(chip), 1);
	if (rc) {
		pr_err("failed to read IMA_ERR_STS, rc=%d\n", rc);
		return rc;
	}

	rc = fg_read(chip, &exp_sts,
			MEM_INTF_IMA_EXP_STS(chip), 1);
	if (rc) {
		pr_err("Error in reading IMA_EXP_STS, rc=%d\n", rc);
		return rc;
	}


	if (check_hw_sts) {
		rc = fg_read(chip, &hw_sts,
				MEM_INTF_IMA_HW_STS(chip), 1);
		if (rc) {
			pr_err("Error in reading IMA_HW_STS, rc=%d\n", rc);
			return rc;
		}
		/*
		 * Lower nibble should be equal to upper nibble before SRAM
		 * transactions begins from SW side. If they are unequal, then
		 * the error clear sequence should be run irrespective of IMA
		 * exception errors.
		 */
		if ((hw_sts & 0x0f) != hw_sts >> 4) {
			pr_err("IMA HW not in correct state, hw_sts=%x\n",
					hw_sts);
			run_err_clr_seq = true;
		}
	}

	if (exp_sts & (IACS_ERR_BIT | XCT_ERR_BIT | DATA_RD_ERR_BIT |
		DATA_WR_ERR_BIT | ADDR_BURST_WRAP_BIT | ADDR_RNG_ERR_BIT |
		ADDR_SRC_ERR_BIT)) {
		pr_err("IMA exception bit set, exp_sts=%x\n", exp_sts);
		run_err_clr_seq = true;
	}

	if (run_err_clr_seq) {
		ret = fg_run_iacs_clear_sequence(chip);
		if (!ret)
			return -EAGAIN;
		else
			pr_err("Error clearing IMA exception ret=%d\n", ret);
	}

	return rc;
}

#define RESET_MASK	(BIT(7) | BIT(5))
static int fg_reset(struct fg_chip *chip, bool reset)
{
	return fg_masked_write(chip, SOC_FG_RESET_REG(chip),
		0xff, reset ? RESET_MASK : 0);
}

static int fg_set_param(struct fg_chip *chip, enum fg_sram_param_id id,
		u8 *val);

static void fg_enable_irqs(struct fg_chip *chip, bool enable)
{
	/*
	 * This function is specific to PMI8950 and older hardware
	 * We can safely enable/disable the PMI8950 specific irqs
	 */
	if (!(enable ^ chip->irqs_enabled))
		return;

	if (enable) {
		enable_irq(chip->irqs[DELTA_SOC_IRQ].irq);
		enable_irq_wake(chip->irqs[DELTA_SOC_IRQ].irq);
		if (!chip->full_soc_irq_enabled) {
			enable_irq(chip->irqs[FULL_SOC_IRQ].irq);
			enable_irq_wake(chip->irqs[FULL_SOC_IRQ].irq);
			chip->full_soc_irq_enabled = true;
		}
		enable_irq(chip->irqs[BATT_MISSING_8950_IRQ].irq);
		if (!chip->vbat_low_irq_enabled) {
			enable_irq(chip->irqs[VBATT_LOW_8950_IRQ].irq);
			enable_irq_wake(chip->irqs[VBATT_LOW_8950_IRQ].irq);
			chip->vbat_low_irq_enabled = true;
		}
		enable_irq(chip->irqs[EMPTY_SOC_IRQ].irq);
		enable_irq_wake(chip->irqs[EMPTY_SOC_IRQ].irq);
		chip->irqs_enabled = true;
	} else {
		disable_irq_wake(chip->irqs[DELTA_SOC_IRQ].irq);
		disable_irq_nosync(chip->irqs[DELTA_SOC_IRQ].irq);
		if (chip->full_soc_irq_enabled) {
			disable_irq_wake(chip->irqs[FULL_SOC_IRQ].irq);
			disable_irq_nosync(chip->irqs[FULL_SOC_IRQ].irq);
			chip->full_soc_irq_enabled = false;
		}
		disable_irq(chip->irqs[BATT_MISSING_8950_IRQ].irq);
		if (chip->vbat_low_irq_enabled) {
			disable_irq_wake(chip->irqs[VBATT_LOW_8950_IRQ].irq);
			disable_irq_nosync(chip->irqs[VBATT_LOW_8950_IRQ].irq);
			chip->vbat_low_irq_enabled = false;
		}
		disable_irq_wake(chip->irqs[EMPTY_SOC_IRQ].irq);
		disable_irq_nosync(chip->irqs[EMPTY_SOC_IRQ].irq);
		chip->irqs_enabled = false;
	}
}

static int fg_hw_init(struct fg_chip *chip);

#define EN_WR_FGXCT_PRD		BIT(6)
#define EN_RD_FGXCT_PRD		BIT(5)
#define FG_RESTART_TIMEOUT_MS	12000
static int fg_check_ima_error_handling(struct fg_chip *chip)
{
	int rc;
	u8 buf[4] = {0, 0, 0, 0};
	fg_enable_irqs(chip, false);

	/* Acquire IMA access forcibly from FG ALG */
	rc = fg_masked_write(chip, MEM_INTF_IMA_CFG(chip),
			EN_WR_FGXCT_PRD | EN_RD_FGXCT_PRD,
			EN_WR_FGXCT_PRD | EN_RD_FGXCT_PRD);
	if (rc) {
		dev_err(chip->dev, "Error in writing to IMA_CFG, rc=%d\n", rc);
		goto out;
	}

	/* Release the IMA access now so that FG reset can go through */
	rc = fg_masked_write(chip, MEM_INTF_IMA_CFG(chip),
			EN_WR_FGXCT_PRD | EN_RD_FGXCT_PRD, 0);
	if (rc) {
		dev_err(chip->dev, "Error in writing to IMA_CFG, rc=%d\n", rc);
		goto out;
	}

	/* Assert FG reset */
	rc = fg_reset(chip, true);
	if (rc) {
		dev_err(chip->dev, "Couldn't reset FG\n");
		goto out;
	}

	/* Deassert FG reset */
	rc = fg_reset(chip, false);
	if (rc) {
		dev_err(chip->dev, "Couldn't clear FG reset\n");
		goto out;
	}

	/* Wait for at least a FG cycle before doing SRAM access */
	msleep(2000);

	fg_hw_init(chip);

out:
	rc = fg_set_param(chip, FG_DATA_VINT_ERR, buf);
	if (rc < 0)
		dev_err(chip->dev, "Error in clearing VACT_INT_ERR, rc=%d\n",
				rc);
	fg_enable_irqs(chip, true);
	return rc;
}

#define FGXCT_PRD		BIT(7)
#define ALG_ST_CHECK_COUNT	20
static int fg_check_alg_status(struct fg_chip *chip)
{
	int rc = 0, timeout = ALG_ST_CHECK_COUNT, count = 0;
	u8 ima_opr_sts, alg_sts = 0, temp = 0;

	if (!chip->reset_on_lockup)  {
		pr_err("FG lockup detection cannot be run\n");
		return 0;
	}

	rc = fg_read(chip, &alg_sts, SOC_ALG_STATUS_REG(chip), 1);
	if (rc) {
		dev_err(chip->dev, "Error in reading SOC_ALG_ST, rc=%d\n", rc);
		return rc;
	}

	do {
		rc = fg_read(chip, &ima_opr_sts,
			MEM_INTF_IMA_OPR_STS(chip), 1);
		if (!rc && !(ima_opr_sts & FGXCT_PRD))
			break;

		if (rc) {
			dev_err(chip->dev, "Error in reading IMA_OPR_STS, rc=%d\n",
				rc);
			break;
		}

		rc = fg_read(chip, &temp, SOC_ALG_STATUS_REG(chip),
			1);
		if (rc) {
			dev_err(chip->dev, "Error in reading SOC_ALG_ST, rc=%d\n",
				rc);
			break;
		}

		if ((ima_opr_sts & FGXCT_PRD) && (temp == alg_sts))
			count++;

		/* Wait for ~10ms while polling ALG_ST & IMA_OPR_STS */
		usleep_range(9000, 11000);
	} while (--timeout);

	if (count == ALG_ST_CHECK_COUNT) {
		/* If we are here, that means FG ALG is stuck */
		dev_err(chip->dev, "ALG is stuck\n");
		fg_check_ima_error_handling(chip);
		rc = -EBUSY;
	}
	return rc;
}

static int fg_check_iacs_ready(struct fg_chip *chip)
{
	int rc = 0, timeout = 250;
	u8 ima_opr_sts = 0;

	/*
	 * Additional delay to make sure IACS ready bit is set after
	 * Read/Write operation.
	 */

	usleep_range(30, 35);
	do {
		rc = fg_read(chip, &ima_opr_sts,
			MEM_INTF_IMA_OPR_STS(chip), 1);
		if (!rc && (ima_opr_sts & IMA_IACS_RDY))
			break;
		/* delay for iacs_ready to be asserted */
		usleep_range(5000, 7000);
	} while (--timeout && !rc);

	if (!timeout || rc) {
		dev_err(chip->dev, "IACS_RDY not set, ima_opr_sts: %x\n",
				ima_opr_sts);
		rc = fg_check_alg_status(chip);
		if (rc && rc != -EBUSY)
			dev_err(chip->dev, "Couldn't check FG ALG status, rc=%d\n",
				rc);
		/* perform IACS_CLR sequence */
		fg_check_and_clear_ima_errors(chip, false);
		return -EBUSY;
	}

	return 0;
}

#define INTF_CTL_BURST		BIT(7)
#define INTF_CTL_WR_EN		BIT(6)
static int fg_config_access(struct fg_chip *chip, bool write,
		bool burst)
{
	int rc;
	u8 intf_ctl;

	intf_ctl = (write ? INTF_CTL_WR_EN : 0) | (burst ? INTF_CTL_BURST : 0);

	rc = fg_write(chip, &intf_ctl, MEM_INTF_CTL(chip), 1);
	if (rc) {
		pr_err("failed to set mem access bit\n");
		return -EIO;
	}

	return rc;
}

static int fg_set_ram_addr(struct fg_chip *chip, u16 *address)
{
	int rc;

	rc = fg_write(chip, (u8 *) address, MEM_INTF_ADDR_LSB(chip), 2);
	if (rc) {
		pr_err("spmi write failed: addr=%03X, rc=%d\n",
				MEM_INTF_ADDR_LSB(chip), rc);
		return rc;
	}

	return rc;
}

#define BUF_LEN		4
static int fg_sub_sram_read(struct fg_chip *chip, u8 *val, u16 address, int len,
		int offset)
{
	int rc, total_len;
	u8 *rd_data = val;

	rc = fg_config_access(chip, 0, (len > 4));
	if (rc)
		return rc;

	rc = fg_set_ram_addr(chip, &address);
	if (rc)
		return rc;

	total_len = len;
	while (len > 0) {
		if (!offset) {
			rc = fg_read(chip, rd_data, MEM_INTF_RD_DATA0(chip),
							min(len, BUF_LEN));
		} else {
			rc = fg_read(chip, rd_data,
				MEM_INTF_RD_DATA0(chip) + offset,
				min(len, BUF_LEN - offset));

			/* manually set address to allow continous reads */
			address += BUF_LEN;

			rc = fg_set_ram_addr(chip, &address);
			if (rc)
				return rc;
		}
		if (rc) {
			pr_err("spmi read failed: addr=%03x, rc=%d\n",
				MEM_INTF_RD_DATA0(chip) + offset, rc);
			return rc;
		}
		rd_data += (BUF_LEN - offset);
		len -= (BUF_LEN - offset);
		offset = 0;
	}

	return rc;
}


#define IACS_SLCT			BIT(5)
static int __fg_interleaved_sram_write(struct fg_chip *chip, u8 *val,
				u16 address, int len, int offset)
{
	int rc = 0, i;
	u8 *word = val, byte_enable = 0, num_bytes = 0;

	while (len > 0) {
			num_bytes = (offset + len) > BUF_LEN ?
				(BUF_LEN - offset) : len;
			/* write to byte_enable */
			for (i = offset; i < (offset + num_bytes); i++)
				byte_enable |= BIT(i);

			rc = fg_write(chip, &byte_enable,
				MEM_INTF_IMA_BYTE_EN(chip), 1);
			if (rc) {
				pr_err("Unable to write to byte_en_reg rc=%d\n",
								rc);
				return rc;
			}
			/* write data */
		rc = fg_write(chip, word, MEM_INTF_WR_DATA0(chip) + offset,
				num_bytes);
		if (rc) {
			pr_err("spmi write failed: addr=%03x, rc=%d\n",
				MEM_INTF_WR_DATA0(chip) + offset, rc);
			return rc;
		}
		/*
		 * The last-byte WR_DATA3 starts the write transaction.
		 * Write a dummy value to WR_DATA3 if it does not have
		 * valid data. This dummy data is not written to the
		 * SRAM as byte_en for WR_DATA3 is not set.
		 */
		if (!(byte_enable & BIT(3))) {
			u8 dummy_byte = 0x0;
			rc = fg_write(chip, &dummy_byte,
				MEM_INTF_WR_DATA0(chip) + 3, 1);
			if (rc) {
				pr_err("Unable to write dummy-data to WR_DATA3 rc=%d\n",
									rc);
				return rc;
			}
		}

		rc = fg_check_iacs_ready(chip);
		if (rc) {
			pr_err("IACS_RDY failed post write to address %x offset %d rc=%d\n",
				address, offset, rc);
			return rc;
		}

		/* check for error condition */
		rc = fg_check_and_clear_ima_errors(chip, false);
		if (rc) {
			pr_err("IMA transaction failed rc=%d", rc);
			return rc;
		}

		word += num_bytes;
		len -= num_bytes;
		offset = byte_enable = 0;
	}

	return rc;
}

#define RIF_MEM_ACCESS_REQ	BIT(7)
static int fg_check_rif_mem_access(struct fg_chip *chip, bool *status)
{
	int rc;
	u8 mem_if_sts;

	rc = fg_read(chip, &mem_if_sts, MEM_INTF_CFG(chip), 1);
	if (rc) {
		pr_err("failed to read rif_mem status rc=%d\n", rc);
		return rc;
	}

	*status = mem_if_sts & RIF_MEM_ACCESS_REQ;
	return 0;
}

#define IMA_REQ_ACCESS		(IACS_SLCT | RIF_MEM_ACCESS_REQ)
static int fg_interleaved_sram_config(struct fg_chip *chip, u8 *val,
		u16 address, int len, int offset, int op)
{
	int rc = 0;
	bool rif_mem_sts = true;
	int time_count;

	/*
	 * Try this no more than 4 times. If RIF_MEM_ACCESS_REQ is not
	 * clear, then return an error instead of waiting for it again.
	 */
	for (time_count = 0; time_count < 4; ++time_count) {
		rc = fg_check_rif_mem_access(chip, &rif_mem_sts);
		if (rc)
			return rc;

		if (!rif_mem_sts)
			break;

		/* Wait for 4ms before reading RIF_MEM_ACCESS_REQ again */
		usleep_range(4000, 4100);
	}
	if  (time_count >= 4) {
		pr_err("Waited for ~16ms polling RIF_MEM_ACCESS_REQ\n");
		return -ETIMEDOUT;
	}


	/* configure for IMA access */
	rc = fg_masked_write(chip, MEM_INTF_CFG(chip),
				IMA_REQ_ACCESS, IMA_REQ_ACCESS);
	if (rc) {
		pr_err("failed to set mem access bit rc = %d\n", rc);
		return rc;
	}

	/* configure for the read/write single/burst mode */
	rc = fg_config_access(chip, op, (offset + len) > 4);
	if (rc) {
		pr_err("failed to set configure memory access rc = %d\n", rc);
		return rc;
	}

	rc = fg_check_iacs_ready(chip);
	if (rc) {
		pr_err("IACS_RDY failed before setting address: %x offset: %d rc=%d\n",
			address, offset, rc);
		return rc;
	}

	/* write addresses to the register */
	rc = fg_set_ram_addr(chip, &address);
	if (rc) {
		pr_err("failed to set SRAM address rc = %d\n", rc);
		return rc;
	}

	rc = fg_check_iacs_ready(chip);
	if (rc)
		pr_err("IACS_RDY failed after setting address: %x offset: %d rc=%d\n",
			address, offset, rc);

	return rc;
}

static int fg_release_access(struct fg_chip *chip)
{
	return fg_masked_write(chip, MEM_INTF_CFG(chip),
			RIF_MEM_ACCESS_REQ, 0);
}

#define FG_MEM_AVAIL_BIT	BIT(0)
static inline int fg_assert_sram_access(struct fg_chip *chip)
{
	int rc;
	u8 mem_if_sts;

	rc = fg_read(chip, &mem_if_sts, INT_RT_STS(chip->mem_base), 1);
	if (rc) {
		pr_err("failed to read mem status rc=%d\n", rc);
		return rc;
	}

	if ((mem_if_sts & FG_MEM_AVAIL_BIT) == 0) {
		pr_err("mem_avail not high: %02x\n", mem_if_sts);
		return -EINVAL;
	}

	rc = fg_read(chip, &mem_if_sts, MEM_INTF_CFG(chip), 1);
	if (rc) {
		pr_err("failed to read mem status rc=%d\n", rc);
		return rc;
	}

	if ((mem_if_sts & RIF_MEM_ACCESS_REQ) == 0) {
		pr_err("rif_mem_access not high: %02x\n", mem_if_sts);
		return -EINVAL;
	}

	return 0;
}

static bool fg_check_sram_access(struct fg_chip *chip)
{
	int rc;
	u8 mem_if_sts;
	bool rif_mem_sts = false;

	rc = fg_read(chip, &mem_if_sts, INT_RT_STS(chip->mem_base), 1);
	if (rc) {
		pr_err("failed to read mem status rc=%d\n", rc);
		return false;
	}

	if ((mem_if_sts & FG_MEM_AVAIL_BIT) == 0)
		return false;

	rc = fg_check_rif_mem_access(chip, &rif_mem_sts);
	if (rc)
		return false;

	return rif_mem_sts;
}

static int fg_req_and_wait_access(struct fg_chip *chip, int timeout)
{
	int rc;
	int tries = 0;

	if (!fg_check_sram_access(chip)) {
		rc = fg_masked_write(chip, MEM_INTF_CFG(chip),
			RIF_MEM_ACCESS_REQ, RIF_MEM_ACCESS_REQ);
		if (rc) {
			pr_err("failed to set mem access bit\n");
			return -EIO;
		}
	}

	do {
		rc = wait_for_completion_interruptible_timeout(
				&chip->sram_access_granted,
				msecs_to_jiffies(timeout));
		if (rc != -ERESTARTSYS) {
			pr_err("transaction timed out rc=%d\n", rc);
			return -ETIMEDOUT;
		}
	} while (tries++ < 2);

	return rc;
}

#define MEM_IF_TIMEOUT_MS	5000
static int fg_conventional_sram_read(struct fg_chip *chip, u8 *val, u16 address,
		int len, int offset, bool keep_access)
{
	int rc = 0, orig_address = address;

	if (offset > 3) {
		pr_err("offset too large %d\n", offset);
		return -EINVAL;
	}

	address = ((orig_address + offset) / 4) * 4;
	offset = (orig_address + offset) % 4;

	mutex_lock(&chip->sram_rw_lock);
	if (!fg_check_sram_access(chip)) {
		rc = fg_req_and_wait_access(chip, MEM_IF_TIMEOUT_MS);
		if (rc)
			goto out;
	}

	rc = fg_sub_sram_read(chip, val, address, len, offset);

out:
	rc = fg_assert_sram_access(chip);
	if (rc) {
		pr_err("memread access not correct\n");
		rc = 0;
	}

	if (!keep_access && !rc) {
		rc = fg_release_access(chip);
		if (rc) {
			pr_err("failed to set mem access bit\n");
			rc = -EIO;
		}
	}

	mutex_unlock(&chip->sram_rw_lock);
	return rc;
}

static int fg_conventional_sram_write(struct fg_chip *chip, u8 *val, u16 address,
		int len, int offset, bool keep_access)
{
	int rc = 0, user_cnt = 0, sublen;
	bool access_configured = false;
	u8 *wr_data = val, word[4];
	u16 orig_address = address;

	if (address < RAM_OFFSET && chip->pmic_version == PMI8950)
		return -EINVAL;

	if (offset > 3)
		return -EINVAL;

	address = ((orig_address + offset) / 4) * 4;
	offset = (orig_address + offset) % 4;

	mutex_lock(&chip->sram_rw_lock);
	if (!fg_check_sram_access(chip)) {
		rc = fg_req_and_wait_access(chip, MEM_IF_TIMEOUT_MS);
		if (rc)
			goto out;
	}

	while (len > 0) {
		if (offset != 0) {
			sublen = min(4 - offset, len);
			rc = fg_sub_sram_read(chip, word, address, 4, 0);
			if (rc)
				goto out;
			memcpy(word + offset, wr_data, sublen);
			/* configure access as burst if more to write */
			rc = fg_config_access(chip, 1, (len - sublen) > 0);
			if (rc)
				goto out;
			rc = fg_set_ram_addr(chip, &address);
			if (rc)
				goto out;
			offset = 0;
			access_configured = true;
		} else if (len >= 4) {
			if (!access_configured) {
				rc = fg_config_access(chip, 1, len > 4);
				if (rc)
					goto out;
				rc = fg_set_ram_addr(chip, &address);
				if (rc)
					goto out;
				access_configured = true;
			}
			sublen = 4;
			memcpy(word, wr_data, 4);
		} else if (len > 0 && len < 4) {
			sublen = len;
			rc = fg_sub_sram_read(chip, word, address, 4, 0);
			if (rc)
				goto out;
			memcpy(word, wr_data, sublen);
			rc = fg_config_access(chip, 1, 0);
			if (rc)
				goto out;
			rc = fg_set_ram_addr(chip, &address);
			if (rc)
				goto out;
			access_configured = true;
		} else {
			pr_err("Invalid length: %d\n", len);
			break;
		}
		rc = fg_write(chip, word, MEM_INTF_WR_DATA0(chip), 4);
		if (rc) {
			pr_err("spmi write failed: addr=%03x, rc=%d\n",
					MEM_INTF_WR_DATA0(chip), rc);
			goto out;
		}
		len -= sublen;
		wr_data += sublen;
		address += 4;
	}

out:
	fg_assert_sram_access(chip);

	if (!keep_access && (user_cnt == 0) && !rc) {
		rc = fg_release_access(chip);
		if (rc) {
			pr_err("failed to set mem access bit\n");
			rc = -EIO;
		}
	}

	mutex_unlock(&chip->sram_rw_lock);
	return rc;
}

#define RETRY_COUNT_MAX	3
static int fg_interleaved_sram_write(struct fg_chip *chip, u8 *val, u16 address,
							int len, int offset)
{
	int rc = 0, ret, orig_address = address;
	u8 count = 0;
	bool retry = false;

	if (address < RAM_OFFSET && chip->pmic_version == PMI8950) {
		pr_err("invalid addr = %x\n", address);
		return -EINVAL;
	}

	if (offset > 3) {
		pr_err("offset too large %d\n", offset);
		return -EINVAL;
	}

	address = ((orig_address + offset) / 4) * 4;
	offset = (orig_address + offset) % 4;

	mutex_lock(&chip->sram_rw_lock);

retry:
	if (count >= RETRY_COUNT_MAX) {
		pr_err("Retried writing 3 times\n");
		retry = false;
		goto out;
	}

	rc = fg_interleaved_sram_config(chip, val, address, len, offset, 1);
	if (rc) {
		pr_err("failed to configure SRAM for IMA rc = %d\n", rc);
		retry = true;
		count++;
		goto out;
	}

	/* write data */
	rc = __fg_interleaved_sram_write(chip, val, address, len, offset);
	if (rc) {
		count++;
		if ((rc == -EAGAIN) && (count < RETRY_COUNT_MAX)) {
			pr_err("IMA access failed retry_count = %d\n", count);
			goto retry;
		} else {
			pr_err("failed to write SRAM address rc = %d\n", rc);
			retry = true;
			goto out;
		}
	}

out:
	/* Release IMA access */
	ret = fg_masked_write(chip, MEM_INTF_CFG(chip), IMA_REQ_ACCESS, 0);
	if (ret)
		pr_err("failed to reset IMA access bit ret = %d\n", ret);

	if (retry) {
		retry = false;
		goto retry;
	}

	mutex_unlock(&chip->sram_rw_lock);
	return rc;
}

static int fg_sram_write(struct fg_chip *chip, u8 *val, u16 address,
		int len, int offset, bool keep_access)
{
	if (chip->ima_supported)
		return fg_interleaved_sram_write(chip, val, address,
				len, offset);
	else
		return fg_conventional_sram_write(chip, val, address,
				len, offset, keep_access);
}

static void fg_encode(struct fg_sram_param param, int val, u8 *buf)
{
	if (!param.encode)
		return;
	param.encode(param, val, buf);
}

static int fg_set_param(struct fg_chip *chip, enum fg_sram_param_id id,
		u8 *val)
{
	struct fg_sram_param param = chip->param[id];

	if (!param.address || !param.length)
		return -EINVAL;

	return fg_sram_write(chip, val, param.address, param.length,
			param.offset, false);
}

static int __fg_interleaved_sram_read(struct fg_chip *chip, u8 *val,
		u16 address, int len, int offset)
{
	int rc = 0, total_len;
	u8 *rd_data = val, num_bytes;

	total_len = len;
	while (len > 0) {
		num_bytes = (offset + len) > BUF_LEN ? (BUF_LEN - offset) : len;
		rc = fg_read(chip, rd_data,
				MEM_INTF_RD_DATA0(chip) + offset,
								num_bytes);
		if (rc) {
			pr_err("spmi read failed: addr=%03x, rc=%d\n",
				MEM_INTF_RD_DATA0(chip) + offset,
				rc);
			return rc;
		}

		rd_data += num_bytes;
		len -= num_bytes;
		offset = 0;

		rc = fg_check_iacs_ready(chip);
		if (rc) {
			pr_err("IACS_RDY failed post read for address %x offset %d rc=%d\n",
					address, offset, rc);
			return rc;
		}

		/* check for error condition */
		rc = fg_check_and_clear_ima_errors(chip, false);
		if (rc) {
			pr_err("IMA transaction failed rc=%d", rc);
			return rc;
		}

		if (len && (len + offset) < BUF_LEN) {
			/* move to single mode */
			u8 intr_ctl = 0;

			rc = fg_write(chip, &intr_ctl, MEM_INTF_CTL(chip), 1);
			if (rc) {
				pr_err("failed to move to single mode rc=%d\n",
					rc);
				return -EIO;
			}
		}
	}

	return rc;
}

static int fg_interleaved_sram_read(struct fg_chip *chip, u8 *val, u16 address,
		int len, int offset)
{
	int rc = 0, orig_address = address, ret = 0;
	u8 start_beat_count, end_beat_count, count = 0;
	const u8 BEAT_COUNT_MASK = 0x0f;
	bool retry = false;

	if (offset > 3) {
		pr_err("offset too large %d\n", offset);
		return -EINVAL;
	}

	address = ((orig_address + offset) / 4) * 4;
	offset = (orig_address + offset) % 4;

	if (address < RAM_OFFSET && chip->pmic_version == PMI8950) {
		/*
		 * OTP memory reads need a conventional memory access, do a
		 * conventional read when SRAM offset < RAM_OFFSET.
		 */
		rc = fg_conventional_sram_read(chip, val, address, len, offset,
						0);
		if (rc)
			pr_err("Failed to read OTP memory %d\n", rc);
		return rc;
	}

	mutex_lock(&chip->sram_rw_lock);

retry:
	if (count >= RETRY_COUNT_MAX) {
		pr_err("Retried reading 3 times\n");
		retry = false;
		goto out;
	}

	rc = fg_interleaved_sram_config(chip, val, address, len, offset, 0);
	if (rc) {
		pr_err("failed to configure SRAM for IMA rc = %d\n", rc);
		retry = true;
		count++;
		goto out;
	}

	/* read the start beat count */
	rc = fg_read(chip, &start_beat_count,
			MEM_INTF_BEAT_COUNT(chip), 1);
	if (rc) {
		pr_err("failed to read beat count rc=%d\n", rc);
		retry = true;
		count++;
		goto out;
	}

	/* read data */
	rc = __fg_interleaved_sram_read(chip, val, address, len, offset);
	if (rc) {
		count++;
		if (rc == -EAGAIN) {
			pr_err("IMA access failed retry_count = %d\n", count);
			goto retry;
		} else {
			pr_err("failed to read SRAM address rc = %d\n", rc);
			retry = true;
			goto out;
		}
	}

	/* read the end beat count */
	rc = fg_read(chip, &end_beat_count,
			MEM_INTF_BEAT_COUNT(chip), 1);
	if (rc) {
		pr_err("failed to read beat count rc=%d\n", rc);
		retry = true;
		count++;
		goto out;
	}

	start_beat_count &= BEAT_COUNT_MASK;
	end_beat_count &= BEAT_COUNT_MASK;
	if (start_beat_count != end_beat_count) {
		retry = true;
		count++;
	}
out:
	ret = fg_release_access(chip);
	if (ret)
		pr_err("failed to reset IMA access bit ret = %d\n", ret);

	if (retry) {
		retry = false;
		goto retry;
	}

	mutex_unlock(&chip->sram_rw_lock);
	return rc;
}

static int fg_sram_read(struct fg_chip *chip, u8 *val, u16 address,
		int len, int offset, bool keep_access)
{
	if (chip->ima_supported)
		return fg_interleaved_sram_read(chip, val, address,
				len, offset);
	else
		return fg_conventional_sram_read(chip, val, address,
				len, offset, keep_access);
}

static int fg_get_param(struct fg_chip *chip, enum fg_sram_param_id id,
		int *val)
{
	struct fg_sram_param param = chip->param[id];
	u8 buf[4];
	int rc;

	if (id < 0 || id >= FG_PARAM_MAX || param.length > ARRAY_SIZE(buf))
		return -EINVAL;

	if (!param.address || !param.length)
		return -EINVAL;

	switch (param.type) {
	case SRAM_PARAM:
		rc = fg_sram_read(chip, buf, param.address, param.length, param.offset,
				false);
		break;
	case BATT_BASE_PARAM:
		rc = fg_read(chip, buf, chip->batt_base + param.address, param.length);
		break;
	case SOC_BASE_PARAM:
		rc = fg_read(chip, buf, chip->soc_base + param.address, param.length);
		break;
	case MEM_IF_PARAM:
		rc = fg_read(chip, buf, chip->mem_base + param.address, param.length);
		break;
	default:
		pr_err("Invalid param type: %d\n", param.type);
		return -EINVAL;
	}
	if (rc)
		return rc;
	if (param.decode)
		*val = param.decode(param, buf);
	else
		*val = fg_decode_default(param, buf);

	return 0;
}

static int fg_sram_masked_write(struct fg_chip *chip, u16 addr,
		u8 mask, u8 val, u8 offset)
{
	int rc = 0;
	u8 reg[4];

	rc = fg_sram_read(chip, reg, addr, 4, 0, 1);
	if (rc)
		return rc;

	reg[offset] &= ~mask;
	reg[offset] |= val & mask;

	rc = fg_sram_write(chip, reg, addr, 4, 0, 0);

	return rc;
}

#define FULL_CAPACITY	100
#define FULL_SOC_RAW	255
static int fg_get_capacity(struct fg_chip *chip, int *val)
{
	u8 cap[2];
	int error = fg_read(chip, cap, BATT_CAPACITY_REG(chip), 2);
	if (error)
		return error;
	//choose lesser of two
	if (cap[0] != cap[1]) {
		dev_warn(chip->dev, "cap not same\n");
		cap[0] = cap[0] < cap[1] ? cap[0] : cap[1];
	}

	*val = DIV_ROUND_CLOSEST(cap[0] * FULL_CAPACITY, FULL_SOC_RAW);
	return 0;
}

static int fg_get_temperature(struct fg_chip *chip, int *val)
{
	int rc, temp;
	int cold = chip->dt.cold_temp;
	int cool = chip->dt.cool_temp;

	rc = fg_get_param(chip, FG_DATA_BATT_TEMP, &temp);
	if (rc) {
		pr_err("failed to read temperature\n");
		return rc;
	}
	if (temp < 0 && chip->pmic_version == PMI8950)
			temp += -50 * (cool - temp) / (cool - cold);
	*val = temp;
	return 0;
}

static int fg_get_health_status(struct fg_chip *chip)
{
	int temp;
	int rc;
	rc = fg_get_temperature(chip, &temp);
	if (rc) {
		return rc;
	}

	if (temp >= chip->dt.hot_temp)
		chip->health = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (temp <= chip->dt.cold_temp)
		chip->health = POWER_SUPPLY_HEALTH_COLD;
	else
		chip->health = POWER_SUPPLY_HEALTH_GOOD;
	return chip->health;
}

#define BATT_MISSING_STS BIT(6)
static bool is_battery_missing(struct fg_chip *chip)
{
	int rc;
	u8 fg_batt_sts;

	rc = fg_read(chip, &fg_batt_sts,
				 INT_RT_STS(chip->batt_base), 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->batt_base), rc);
		return false;
	}

	return (fg_batt_sts & BATT_MISSING_STS) ? true : false;
}

static bool is_usb_present(struct fg_chip *chip)
{
	union power_supply_propval prop = {0,};
	if (!chip->usb_psy)
		chip->usb_psy = power_supply_get_by_name("usb");

	if (chip->usb_psy)
		power_supply_get_property(chip->usb_psy,
				POWER_SUPPLY_PROP_PRESENT, &prop);
	return prop.intval != 0;
}

static bool is_dc_present(struct fg_chip *chip)
{
	union power_supply_propval prop = {0,};
	if (!chip->dc_psy)
		chip->dc_psy = power_supply_get_by_name("dc");

	if (chip->dc_psy)
		power_supply_get_property(chip->dc_psy,
				POWER_SUPPLY_PROP_PRESENT, &prop);
	return prop.intval != 0;
}

static bool is_input_present(struct fg_chip *chip)
{
	return is_usb_present(chip) || is_dc_present(chip);
}

static bool is_otg_present(struct fg_chip *chip)
{
	return false;
	/*TODO: OTG detection
	union power_supply_propval prop = {0,};
	if (!chip->usb_psy)
		chip->usb_psy = power_supply_get_by_name("usb");
	if (chip->usb_psy)
		power_supply_get_property(chip->usb_psy,
				POWER_SUPPLY_PROP_USB_OTG, &prop);
	return prop.intval != 0;
	*/
}

static bool is_charger_available(struct fg_chip *chip)
{
	if (!chip->batt_psy)
		chip->batt_psy = power_supply_get_by_name("battery");

	if (!chip->batt_psy)
		return false;

	return true;
}

static bool is_parallel_charger_available(struct fg_chip *chip)
{
	if (!chip->parallel_psy)
		chip->parallel_psy = power_supply_get_by_name("parallel");

	if (!chip->parallel_psy)
		return false;

	return true;
}

static int fg_esr_fcc_config(struct fg_chip *chip)
{
	union power_supply_propval prop = {0, };
	int rc;
	bool parallel_en = false;

	if (is_parallel_charger_available(chip)) {
		rc = power_supply_get_property(chip->parallel_psy,
			POWER_SUPPLY_PROP_ONLINE, &prop);
		if (rc < 0) {
			pr_err("failed to get charging_enabled from parallel_psy, rc=%d\n",
				rc);
			return rc;
		}
		parallel_en = prop.intval;
	}
	dev_warn(chip->dev, "QNOVO IS NOT SUPPORTED!\n");

	if (chip->charge_status == POWER_SUPPLY_STATUS_CHARGING &&
			(parallel_en)) {
		if (chip->esr_fcc_ctrl_en)
			return 0;

		/*
		 * When parallel charging or Qnovo is enabled, configure ESR
		 * FCC to 300mA to trigger an ESR pulse. Without this, FG can
		 * request the main charger to increase FCC when it is supposed
		 * to decrease it.
		 */
		rc = regmap_update_bits(chip->regmap,
				BATT_INFO_ESR_FAST_CRG_CFG(chip),
				ESR_FAST_CRG_IVAL_MASK |
				ESR_FAST_CRG_CTL_EN_BIT,
				ESR_FCC_300MA | ESR_FAST_CRG_CTL_EN_BIT);
		if (rc < 0) {
			dev_err(chip->dev, "failed to write ESR_FCC: %d\n", rc);
			return rc;
		}

		chip->esr_fcc_ctrl_en = true;
	} else {
		if (!chip->esr_fcc_ctrl_en)
			return 0;

		/*
		 * If we're here, then it means either the device is not in
		 * charging state or parallel charging / Qnovo is disabled.
		 * Disable ESR fast charge current control in SW.
		 */
		rc = regmap_update_bits(chip->regmap,
				BATT_INFO_ESR_FAST_CRG_CFG(chip),
				ESR_FAST_CRG_CTL_EN_BIT, 0);
		if (rc < 0) {
			dev_err(chip->dev, "failed to write ESR_FCC: %d\n", rc);
			return rc;
		}

		chip->esr_fcc_ctrl_en = false;
	}

	return 0;
}

#define MAXRSCHANGE_REG		0x434
#define ESR_VALUE_OFFSET	1
#define ESR_STRICT_VALUE	0x4120391F391F3019
#define ESR_DEFAULT_VALUE	0x58CD4A6761C34A67
static void fg_update_esr_values(struct fg_chip* chip)
{
	union power_supply_propval prop = {0, };
	u64 esr_value = 0;
	u64 esr_readback = 0;
	int rc = 0;

	if (!is_charger_available(chip))
		return;

	power_supply_get_property(chip->batt_psy,
			POWER_SUPPLY_PROP_CHARGE_TYPE, &prop);

	rc = fg_sram_read(chip, (u8 *)&esr_readback, MAXRSCHANGE_REG, 8,
			ESR_VALUE_OFFSET, 0);
	if (rc)
		dev_err(chip->dev, "failed to read esr: %d\n", rc);

	if ((prop.intval == POWER_SUPPLY_CHARGE_TYPE_TRICKLE &&
			chip->status == POWER_SUPPLY_STATUS_CHARGING) ||
		(chip->status == POWER_SUPPLY_STATUS_FULL)) {
		if (esr_readback != ESR_STRICT_VALUE) {
			esr_value = ESR_STRICT_VALUE;
			rc = fg_sram_write(chip, (u8 *)&esr_value,
				MAXRSCHANGE_REG, 8, ESR_VALUE_OFFSET, 0);
			if (rc)
				dev_err(chip->dev, "failed to set ESR: %d\n", rc);
		}
	} else if ((prop.intval != POWER_SUPPLY_CHARGE_TYPE_TRICKLE &&
			chip->status == POWER_SUPPLY_STATUS_CHARGING) ||
		(chip->status == POWER_SUPPLY_STATUS_DISCHARGING)) {
		if (esr_readback != ESR_DEFAULT_VALUE) {
			esr_value = ESR_DEFAULT_VALUE;
			rc = fg_sram_write(chip, (u8 *)&esr_value,
				MAXRSCHANGE_REG, 8, ESR_VALUE_OFFSET, 0);
			if (rc)
				dev_err(chip->dev, "failed to set ESR: %d\n", rc);
		}
	}
}

static void restore_cycle_counter(struct fg_chip *chip)
{
	int rc = 0, i;
	u8 data[2];
	const struct fg_sram_param cyc_ctr = chip->param[FG_DATA_CYCLE_COUNT];

	if (!chip->cyc_ctr.en)
		return;

	mutex_lock(&chip->cyc_ctr.lock);
	for (i = 0; i < BUCKET_COUNT; i++) {
		rc = fg_sram_read(chip, data, cyc_ctr.address + (i / 2),
				cyc_ctr.length, cyc_ctr.offset + (i % 2) * 2,
				false);
		if (rc < 0)
			dev_err(chip->dev, "failed to read bucket %d: %d\n",
					i, rc);
		else
			chip->cyc_ctr.count[i] = data[0] | data[1] << 8;
	}
	mutex_unlock(&chip->cyc_ctr.lock);
}

static void clear_cycle_counter(struct fg_chip *chip)
{
	int rc = 0, i;
	const struct fg_sram_param cyc_ctr = chip->param[FG_DATA_CYCLE_COUNT];

	if (!chip->cyc_ctr.en)
		return;

	mutex_lock(&chip->cyc_ctr.lock);
	memset(chip->cyc_ctr.count, 0, sizeof(chip->cyc_ctr.count));
	for (i = 0; i < BUCKET_COUNT; i++) {
		chip->cyc_ctr.started[i] = false;
		chip->cyc_ctr.last_soc[i] = 0;
	}
	rc = fg_sram_write(chip, (u8 *)&chip->cyc_ctr.count,
			sizeof(chip->cyc_ctr.count) / sizeof(u8 *),
			cyc_ctr.address, cyc_ctr.offset, false);
	if (rc < 0)
		dev_err(chip->dev, "failed to clear cycle counter: %d\n", rc);

	mutex_unlock(&chip->cyc_ctr.lock);
}

static int fg_inc_store_cycle_ctr(struct fg_chip *chip, int bucket)
{
	int rc = 0;
	u16 cyc_count;
	u8 data[2];

	if (bucket < 0 || (bucket > BUCKET_COUNT - 1))
		return 0;

	cyc_count = chip->cyc_ctr.count[bucket];
	cyc_count++;
	data[0] = cyc_count & 0xFF;
	data[1] = cyc_count >> 8;

	rc = fg_sram_write(chip, data,
			chip->param[FG_DATA_CYCLE_COUNT].address + (bucket / 2),
			chip->param[FG_DATA_CYCLE_COUNT].length,
			chip->param[FG_DATA_CYCLE_COUNT].offset + (bucket % 2) * 2,
			false);
	if (rc) {
		dev_err(chip->dev, "failed to write BATT_CYCLE[%d]: %d\n",
			bucket, rc);
		return rc;
	}

	chip->cyc_ctr.count[bucket] = cyc_count;

	return rc;
}

static void fg_cycle_counter_update(struct fg_chip *chip)
{
	int rc = 0, bucket, i, batt_soc;

	if (!chip->cyc_ctr.en)
		return;

	mutex_lock(&chip->cyc_ctr.lock);
	rc = fg_get_param(chip, FG_DATA_BATT_SOC, &batt_soc);
	if (rc) {
		dev_err(chip->dev, "failed to read battery soc rc: %d\n", rc);
		goto out;
	}

	/* We need only the most significant byte here */
	batt_soc = (u32)batt_soc >> 24;

	/* Find out which bucket the SOC falls in */
	bucket = batt_soc / BUCKET_SOC_PCT;

	if (chip->charge_status == POWER_SUPPLY_STATUS_CHARGING) {
		if (!chip->cyc_ctr.started[bucket]) {
			chip->cyc_ctr.started[bucket] = true;
			chip->cyc_ctr.last_soc[bucket] = batt_soc;
		}
	} else if (chip->charge_done || !is_input_present(chip)) {
		for (i = 0; i < BUCKET_COUNT; i++) {
			if (chip->cyc_ctr.started[i] &&
				batt_soc > chip->cyc_ctr.last_soc[i] + 2) {
				fg_inc_store_cycle_ctr(chip, i);
				chip->cyc_ctr.last_soc[i] = 0;
				chip->cyc_ctr.started[i] = false;
			}
		}
	}
out:
	mutex_unlock(&chip->cyc_ctr.lock);
}

#define TEMP_COUNTER_REG	0x580
#define VBAT_FILTERED_OFFSET	1
#define GAIN_REG		0x424
#define GAIN_OFFSET		1
#define DEF_GAIN_OFFSET		2
#define PICO_UNIT		0xE8D4A51000LL
#define ATTO_UNIT		0xDE0B6B3A7640000LL
#define VBAT_REF		3800000
#define LSB_24B_NUMRTR		596046
#define LSB_24B_DENMTR		1000000
static void fg_iadc_gain_comp(struct fg_chip *chip)
{
	/*
	 * IADC Gain compensation steps:
	 * If Input/OTG absent:
	 *	- read VBAT_FILTERED, KVCOR, GAIN
	 *	- calculate the gain compensation using following formula:
	 *	  gain = (1 + gain) * (1 + kvcor * (vbat_filtered - 3800000)) - 1;
	 * else
	 *	- reset to the default gain compensation
	 */
	u8 reg[4];
	int rc;
	uint64_t vbat_filtered;
	int64_t gain, kvcor, temp, numerator;
	bool input_present = is_input_present(chip);

	struct fg_sram_param k_vcor_reg = {
		.address	= 0x484,
		.offset		= 0,
		.length		= 2,
	};

	if (!input_present) {
		/* read VBAT_FILTERED */
		rc = fg_sram_read(chip, reg, TEMP_COUNTER_REG, 3,
						VBAT_FILTERED_OFFSET, 0);
		if (rc) {
			dev_err(chip->dev, "failed to read VBAT: %d\n", rc);
			return;
		}
		temp = (reg[2] << 16) | (reg[1] << 8) | reg[0];
		vbat_filtered = div_u64((u64)temp * LSB_24B_NUMRTR,
						LSB_24B_DENMTR);

		/* read K_VCOR */
		rc = fg_sram_read(chip, reg, k_vcor_reg.address,
				k_vcor_reg.length, k_vcor_reg.offset, false);
		if (rc) {
			dev_err(chip->dev, "failed to KVCOR: %d\n", rc);
			return;
		}
		kvcor = fg_decode_float(k_vcor_reg, reg);

		/* calculate gain */
		numerator = (MICRO_UNIT + chip->iadc_comp_data.dfl_gain)
			* (PICO_UNIT + kvcor * (vbat_filtered - VBAT_REF))
			- ATTO_UNIT;
		gain = div64_s64(numerator, PICO_UNIT);

		/* write back gain */
		fg_encode_float(k_vcor_reg, gain, reg);
		rc = fg_sram_write(chip, reg, GAIN_REG, 2, GAIN_OFFSET, 0);
		if (rc) {
			dev_err(chip->dev, "failed to set gain reg: %d\n", rc);
			return;
		}

		chip->iadc_comp_data.gain_active = true;
	} else {
		/* reset gain register */
		rc = fg_sram_write(chip, chip->iadc_comp_data.dfl_gain_reg,
						GAIN_REG, 2, GAIN_OFFSET, 0);
		if (rc) {
			dev_err(chip->dev, "failed to set gain comp: %d\n", rc);
			return;
		}

		chip->iadc_comp_data.gain_active = false;
	}
}

#define KI_COEFF_LOW_DISCHG_DEFAULT	800
#define KI_COEFF_MED_DISCHG_DEFAULT	1500
#define KI_COEFF_HI_DISCHG_DEFAULT	2200
static int fg_adjust_ki_coeff_dischg(struct fg_chip *chip)
{
	int rc, i, msoc;
	int ki_coeff_low = KI_COEFF_LOW_DISCHG_DEFAULT;
	int ki_coeff_med = KI_COEFF_MED_DISCHG_DEFAULT;
	int ki_coeff_hi = KI_COEFF_HI_DISCHG_DEFAULT;
	u8 val;

	if (!chip->ki_coeff_dischg_en)
		return 0;

	rc = fg_get_capacity(chip, &msoc);
	if (rc < 0) {
		dev_err(chip->dev, "failed to get capacity: %d\n", rc);
		return rc;
	}

	if (chip->charge_status == POWER_SUPPLY_STATUS_DISCHARGING) {
		for (i = KI_COEFF_SOC_LEVELS - 1; i >= 0; i--) {
			if (msoc < chip->dt.ki_coeff_soc[i]) {
				ki_coeff_low = chip->dt.ki_coeff_low_dischg[i];
				ki_coeff_med = chip->dt.ki_coeff_med_dischg[i];
				ki_coeff_hi = chip->dt.ki_coeff_hi_dischg[i];
			}
		}
	}

	fg_encode(chip->param[FG_SETTING_KI_COEFF_LOW_DISCHG], ki_coeff_low,
			&val);
	rc = fg_set_param(chip, FG_SETTING_KI_COEFF_DISCHG, &val);
	if (rc < 0) {
		dev_err(chip->dev, "failed to set ki_coeff_low: %d\n", rc);
		return rc;
	}

	fg_encode(chip->param[FG_SETTING_KI_COEFF_MED_DISCHG], ki_coeff_med,
			&val);
	rc = fg_set_param(chip, FG_SETTING_KI_COEFF_MED_DISCHG, &val);
	if (rc < 0) {
		dev_err(chip->dev, "failed to set ki_coeff_med: %d\n", rc);
		return rc;
	}

	fg_encode(chip->param[FG_SETTING_KI_COEFF_HI_DISCHG], ki_coeff_hi,
			&val);
	rc = fg_set_param(chip, FG_SETTING_KI_COEFF_HI_DISCHG, &val);
	if (rc < 0) {
		dev_err(chip->dev, "failed to set ki_coeff_hi: %d\n", rc);
		return rc;
	}

	return 0;
}

#define KI_COEFF_FULL_SOC_DEFAULT	733
static int fg_adjust_ki_coeff_full_soc(struct fg_chip *chip, int batt_temp)
{
	int rc, ki_coeff_full_soc;
	u8 val;

	if (batt_temp < 0)
		ki_coeff_full_soc = 0;
	else if (chip->charge_status == POWER_SUPPLY_STATUS_DISCHARGING)
		ki_coeff_full_soc = chip->dt.ki_coeff_full_soc_dischg;
	else
		ki_coeff_full_soc = KI_COEFF_FULL_SOC_DEFAULT;

	if (chip->ki_coeff_full_soc == ki_coeff_full_soc)
		return 0;

	fg_encode(chip->param[FG_SETTING_KI_COEFF_FULL_SOC], ki_coeff_full_soc,
			&val);
	rc = fg_set_param(chip, FG_SETTING_KI_COEFF_FULL_SOC, &val);
	if (rc) {
		dev_err(chip->dev, "failed to write ki_coeff_full_soc: %d\n",
				rc);
		return rc;
	}

	chip->ki_coeff_full_soc = ki_coeff_full_soc;
	return 0;
}

static int fg_configure_full_soc(struct fg_chip *chip, int bsoc)
{
	int rc;
	u8 full_soc[2] = {0xff, 0xff};

	/*
	 * Once SOC masking condition is cleared, FULL_SOC and MONOTONIC_SOC
	 * needs to be updated to reflect the same. Write battery SOC to
	 * FULL_SOC and write a full value to MONOTONIC_SOC.
	 */
	rc = fg_set_param(chip, FG_PARAM_MONOTONIC_SOC, full_soc);
	if (rc) {
		dev_err(chip->dev, "failed to write monotonic_soc: %d\n", rc);
		return rc;
	}

	rc = fg_set_param(chip, FG_DATA_FULL_SOC, (u8 *)&bsoc);
	if (rc) {
		dev_err(chip->dev, "failed to write full_soc: %d\n", rc);
		return rc;
	}

	return 0;
}

#define AUTO_RECHG_VOLT_LOW_LIMIT_MV	3700
static int fg_charge_full_update(struct fg_chip *chip)
{
	union power_supply_propval prop = {0, };
	int rc, msoc, bsoc, recharge_soc;

	if (!batt_psy_initialized(chip))
		return 0;

	/*
	 * Disable BSOC irq on gen3 hardware to avoid it triggering
	 * during writes to the full_soc reg
	 * It will be re-enabled elesewhere properly when appropriate
	 */
	if (chip->pmic_version != PMI8950)
		disable_irq(chip->irqs[BSOC_DELTA_IRQ].irq);

	rc = power_supply_get_property(chip->batt_psy, POWER_SUPPLY_PROP_HEALTH,
		&prop);
	if (rc) {
		dev_err(chip->dev, "failed to get battery health: %d\n", rc);
		return rc;
	}

	chip->health = prop.intval;
	recharge_soc = chip->dt.recharge_thr;
	rc = fg_get_param(chip, FG_DATA_BATT_SOC, &bsoc);
	if (rc < 0) {
		dev_err(chip->dev, "failed to get batt_soc: %d\n", rc);
		return rc;
	}

	/*
	 * We will be writing bsoc to full_soc
	 * that's why we only need full_soc.length bytes
	 */
	bsoc = (u32)bsoc >> (8 * (4 - chip->param[FG_DATA_FULL_SOC].length));
	rc = fg_get_capacity(chip, &msoc);
	if (rc < 0) {
		dev_err(chip->dev, "failed to fet msoc_raw: %d\n", rc);
		return rc;
	}

	if (chip->charge_done && !chip->charge_full) {
		if (msoc >= 99 && chip->health == POWER_SUPPLY_HEALTH_GOOD)
			chip->charge_full = true;
	} else if ((msoc <= recharge_soc || !chip->charge_done)
			&& chip->charge_full) {
		/*
		 * If charge_done is still set, wait for recharging or
		 * discharging to happen.
		 */
		if (chip->charge_done)
			return rc;

		rc = fg_configure_full_soc(chip, bsoc);
		if (rc < 0)
			return rc;

		chip->charge_full = false;
	}

	return rc;
}

#define RCONN_CONFIG_BIT		BIT(0)
#define PMI8998_PROFILE_INTEGRITY_REG	79
#define RCONN_CONF_STS_OFFSET		0
static int fg_rconn_config_pmi8998(struct fg_chip *chip)
{
	int rc, esr_uohms;
	u64 scaling_factor;
	u32 val = 0;
	u8 buf[4];

	if (!chip->batt_info.rconn_mohm)
		return 0;

	rc = fg_sram_read(chip, (u8 *)&val, PMI8998_PROFILE_INTEGRITY_REG,
			1, RCONN_CONF_STS_OFFSET, false);
	if (rc < 0) {
		pr_err("Error in reading RCONN_CONF_STS, rc=%d\n", rc);
		return rc;
	}

	if (val & RCONN_CONFIG_BIT)
		return 0;

	rc = fg_get_param(chip, FG_DATA_BATT_ESR, &esr_uohms);
	if (rc < 0) {
		pr_err("failed to get ESR, rc=%d\n", rc);
		return rc;
	}

	scaling_factor = div64_u64((u64)esr_uohms * 1000,
				esr_uohms +
				(chip->batt_info.rconn_mohm * 1000));

	rc = fg_get_param(chip, FG_SETTING_RSLOW_CHG, &val);
	if (rc < 0) {
		pr_err("Error in reading ESR_RSLOW_CHG, rc=%d\n", rc);
		return rc;
	}

	val *= scaling_factor;
	do_div(val, 1000);
	fg_encode(chip->param[FG_SETTING_RSLOW_CHG], val, buf);
	rc = fg_set_param(chip, FG_SETTING_RSLOW_CHG, buf);
	if (rc < 0) {
		pr_err("Error in writing ESR_RSLOW_CHG, rc=%d\n", rc);
		return rc;
	}

	fg_encode(chip->param[FG_SETTING_RSLOW_DISCHG], val, buf);
	rc = fg_set_param(chip, FG_SETTING_RSLOW_DISCHG, buf);
	if (rc < 0) {
		pr_err("Error in reading ESR_RSLOW_DISCHG_OFFSET, rc=%d\n", rc);
		return rc;
	}

	val *= scaling_factor;
	do_div(val, 1000);
	fg_encode(chip->param[FG_SETTING_RSLOW_DISCHG], val, buf);
	rc = fg_set_param(chip, FG_SETTING_RSLOW_DISCHG, buf);
	if (rc < 0) {
		pr_err("Error in writing ESR_RSLOW_DISCHG_OFFSET, rc=%d\n", rc);
		return rc;
	}

	val = RCONN_CONFIG_BIT;
	rc = fg_sram_write(chip, (u8 *)&val, PMI8998_PROFILE_INTEGRITY_REG, 1,
			RCONN_CONF_STS_OFFSET, false);
	if (rc < 0) {
		pr_err("Error in writing RCONN_CONF_STS, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int fg_rconn_config_pmi8950(struct fg_chip *chip)
{
	int rs_to_rslow_chg, rs_to_rslow_dischg, batt_esr, rconn_uohm;
	int rc;
	u8 buf[4];

	rc = fg_get_param(chip, FG_DATA_BATT_ESR, &batt_esr);
	if (rc) {
		pr_err("unable to read battery_esr: %d\n", rc);
		return rc;
	}

	rc = fg_get_param(chip, FG_SETTING_RSLOW_DISCHG, &rs_to_rslow_dischg);
	if (rc) {
		pr_err("unable to read rs to rslow dischg: %d\n", rc);
		return rc;
	}

	rc = fg_get_param(chip, FG_SETTING_RSLOW_CHG, &rs_to_rslow_chg);
	if (rc) {
		pr_err("unable to read rs to rslow chg: %d\n", rc);
		return rc;
	}

	rconn_uohm = chip->batt_info.rconn_mohm * 1000;
	rs_to_rslow_dischg = div64_s64(rs_to_rslow_dischg * batt_esr,
					batt_esr + rconn_uohm);
	rs_to_rslow_chg = div64_s64(rs_to_rslow_chg * batt_esr,
					batt_esr + rconn_uohm);

	fg_encode(chip->param[FG_SETTING_RSLOW_CHG], rs_to_rslow_chg, buf);
	rc = fg_set_param(chip, FG_SETTING_RSLOW_CHG, buf);
	if (rc) {
		pr_err("unable to write rs_to_rslow_chg: %d\n", rc);
		return rc;
	}

	fg_encode(chip->param[FG_SETTING_RSLOW_DISCHG], rs_to_rslow_dischg,
			buf);
	rc = fg_set_param(chip, FG_SETTING_RSLOW_DISCHG, buf);
	if (rc) {
		pr_err("unable to write rs_to_rslow_dischg: %d\n", rc);
		return rc;
	}
	return 0;
}

#define FG_ADC_CONFIG_REG		0x4b8
#define FG_ALG_SYSCTL_REG		0x4b0
#define FG_CYCLE_MS			1500
#define BCL_FORCED_HPM_IN_CHARGE	BIT(2)
#define IRQ_USE_VOLTAGE_HYST_BIT	BIT(0)
#define EMPTY_FROM_VOLTAGE_BIT		BIT(1)
#define EMPTY_FROM_SOC_BIT		BIT(2)
#define EMPTY_SOC_IRQ_MASK		(IRQ_USE_VOLTAGE_HYST_BIT | \
					EMPTY_FROM_SOC_BIT | \
					EMPTY_FROM_VOLTAGE_BIT)
#define ALERT_CFG_OFFSET		3
#define EXTERNAL_SENSE_SELECT_REG	0x4ac
#define EXTERNAL_SENSE_OFFSET		2
#define EXTERNAL_SENSE_BIT		BIT(2)
#define THERMAL_COEFF_ADDR_PMI8950	0x444
#define THERMAL_COEFF_OFFSET_PMI8950	0x2
#define THERM_DELAY_REG			0x4ac
#define THERM_DELAY_OFFSET		3
#define JEITA_TEMP_REG_PMI8950		0x454
#define JEITA_TEMP_LEN_PMI8950		4
#define JEITA_TEMP_OFF_PMI8950		0
#define RSLOW_CFG_REG			0x538
#define RSLOW_CFG_LEN			1
#define RSLOW_CFG_OFFSET		2
#define RSLOW_CFG_MASK			(BIT(2) | BIT(3) | BIT(4) | BIT(5))
#define RSLOW_CFG_ON_VAL		(BIT(2) | BIT(3))
#define RSLOW_THRESH_FULL_VAL		0xff
static int fg_hw_init_pmi8950(struct fg_chip *chip)
{
	int rc;
	u8 buf[4];

	rc = fg_sram_masked_write(chip, EXTERNAL_SENSE_SELECT_REG,
			EXTERNAL_SENSE_BIT, 0, EXTERNAL_SENSE_OFFSET);
	if (rc) {
		dev_err(chip->dev, "failed to write ext_sense_sel: %d\n", rc);
		return rc;
	}

	if (chip->batt_info.thermal_coeffs_len != 0) {
		rc = fg_sram_write(chip, chip->batt_info.thermal_coeffs,
				THERMAL_COEFF_ADDR_PMI8950,
				chip->batt_info.thermal_coeffs_len,
				THERMAL_COEFF_OFFSET_PMI8950, false);
		if (rc) {
			dev_err(chip->dev, "failed to write thermal coeffs to"\
					" batt soc");
		}
	}

	rc = fg_sram_masked_write(chip, THERM_DELAY_REG, THERM_DELAY_MASK,
			0, THERM_DELAY_OFFSET);
	if (rc) {
		dev_err(chip->dev, "failed to write therm_delay rc=%d\n", rc);
		return rc;
	}

	buf[0] =  chip->dt.cool_temp / 10 + 30;
	buf[1] =  chip->dt.warm_temp / 10 + 30;
	buf[2] =  chip->dt.cold_temp / 10 + 30;
	buf[3] =  chip->dt.hot_temp  / 10 + 30;

	rc = fg_sram_write(chip, buf, JEITA_TEMP_REG_PMI8950,
			JEITA_TEMP_LEN_PMI8950, JEITA_TEMP_OFF_PMI8950, false);
	if (rc) {
		dev_err(chip->dev, "failed to write jeita temp settings: %d\n",
				rc);
		return rc;
	}

	if (buf[0] & RSLOW_CFG_ON_VAL)
		chip->rslow_comp.active = true;
	/*
	 * Clear bits 0-2 in 0x4B3 and set them again to make empty_soc irq
	 * trigger again.
	 */
	rc = fg_sram_masked_write(chip, FG_ALG_SYSCTL_REG, EMPTY_SOC_IRQ_MASK,
			0, ALERT_CFG_OFFSET);
	if (rc) {
		dev_err(chip->dev, "failed to write to fg_alg_sysctl rc=%d\n",
				rc);
		return rc;
	}

	/* Wait for a FG cycle before enabling empty soc irq configuration */
	msleep(FG_CYCLE_MS);

	rc = fg_sram_masked_write(chip, FG_ALG_SYSCTL_REG, EMPTY_SOC_IRQ_MASK,
			EMPTY_SOC_IRQ_MASK, ALERT_CFG_OFFSET);
	if (rc) {
		dev_err(chip->dev, "failed to write fg_alg_sysctl: %d\n", rc);
		return rc;
	}

	rc = fg_sram_masked_write(chip, FG_ADC_CONFIG_REG,
			BCL_FORCED_HPM_IN_CHARGE, BCL_FORCED_HPM_IN_CHARGE, 3);
	if (rc) {
		dev_err(chip->dev, "failed to set HPM_IN_CHARGE: %d\n", rc);
		return rc;
	}

	rc = fg_sram_read(chip, buf, RSLOW_CFG_REG, RSLOW_CFG_LEN,
			RSLOW_CFG_OFFSET, false);
	if (rc) {
		dev_err(chip->dev, "failed to read rslow_cfg: %d\n", rc);
		return rc;
	}

	return rc;
}

static int fg_set_esr_timer(struct fg_chip *chip, int cycles_init,
				int cycles_max, bool charging)
{
	int rc, timer_max, timer_init;
	u8 buf[2];

	if (cycles_init < 0 || cycles_max < 0)
		return 0;

	if (charging) {
		timer_max = FG_SETTING_ESR_TIMER_CHG_MAX;
		timer_init = FG_SETTING_ESR_TIMER_CHG_INIT;
	} else {
		timer_max = FG_SETTING_ESR_TIMER_DISCHG_MAX;
		timer_init = FG_SETTING_ESR_TIMER_DISCHG_INIT;
	}

	fg_encode(chip->param[timer_max], cycles_max, buf);
	rc = fg_set_param(chip, timer_max, buf);
	if (rc < 0) {
		dev_err(chip->dev, "failed to write esr_timer_max, rc=%d\n",
			rc);
		return rc;
	}

	fg_encode(chip->param[timer_init], cycles_max, buf);
	rc = fg_set_param(chip, timer_init, buf);
	if (rc < 0) {
		dev_err(chip->dev, "failed to write esr_timer_init, rc=%d\n",
			rc);
		return rc;
	}

	return 0;
}

/* BATT_INFO_ESR_PULL_DN_CFG */
#define ESR_PULL_DOWN_IVAL_MASK			GENMASK(3, 2)
#define ESR_PULL_DOWN_IVAL_SHIFT		2
#define ESR_MEAS_CUR_60MA			0x0
#define ESR_MEAS_CUR_120MA			0x1
#define ESR_MEAS_CUR_180MA			0x2
#define ESR_MEAS_CUR_240MA			0x3
#define ESR_PULL_DOWN_MODE_MASK			GENMASK(1, 0)
#define ESR_NO_PULL_DOWN			0x0
#define ESR_STATIC_PULL_DOWN			0x1
#define ESR_CRG_DSC_PULL_DOWN			0x2
#define ESR_DSC_PULL_DOWN			0x3
static inline void get_esr_meas_current(int curr_ma, u8 *val)
{
	switch (curr_ma) {
	case 60:
		*val = ESR_MEAS_CUR_60MA;
		break;
	case 120:
		*val = ESR_MEAS_CUR_120MA;
		break;
	case 180:
		*val = ESR_MEAS_CUR_180MA;
		break;
	case 240:
		*val = ESR_MEAS_CUR_240MA;
		break;
	default:
		*val = ESR_MEAS_CUR_120MA;
		break;
	};

	*val <<= ESR_PULL_DOWN_IVAL_SHIFT;
}

static int fg_init_iadc_config(struct fg_chip *chip)
{
	/* IADC gain is only for pmi8950 and older */
	u8 reg[2];
	int rc;

	struct fg_sram_param def_gain_reg = {
		.address	= 0x484,
		.offset		= 2,
		.length		= 2,
	}, gain_reg = {
		.address	= 0x424,
		.offset		= 1,
		.length		= 2,
	};

	/* read default gain config */
	rc = fg_sram_read(chip, reg, def_gain_reg.address,
			def_gain_reg.length, def_gain_reg.offset, false);
	if (rc) {
		dev_err(chip->dev, "failed to read default gain: %d\n", rc);
		return rc;
	}

	if (reg[1] || reg[0]) {
		/*
		 * Default gain register has valid value:
		 * - write to gain register.
		 */
		rc = fg_sram_write(chip, reg, gain_reg.address, gain_reg.length,
				gain_reg.offset, false);
		if (rc) {
			dev_err(chip->dev, "failed to write gain: %d\n", rc);
			return rc;
		}
	} else {
		/*
		 * Default gain register is invalid:
		 * - read gain register for default gain value
		 * - write to default gain register.
		 */
		rc = fg_sram_read(chip, reg, gain_reg.address, gain_reg.length,
				gain_reg.offset, false);
		if (rc) {
			dev_err(chip->dev, "failed to read gain: %d\n", rc);
			return rc;
		}
		rc = fg_sram_write(chip, reg, def_gain_reg.address,
				def_gain_reg.length, def_gain_reg.offset, false);
		if (rc) {
			dev_err(chip->dev, "failed to write default gain: %d\n",
					rc);
			return rc;
		}
	}

	chip->iadc_comp_data.dfl_gain_reg[0] = reg[0];
	chip->iadc_comp_data.dfl_gain_reg[1] = reg[1];
	chip->iadc_comp_data.dfl_gain = fg_decode_float(def_gain_reg, reg);

	return 0;
}

static int fg_hw_init_pmi8998(struct fg_chip *chip)
{
	int rc;
	u8 buf[4];

	fg_encode(chip->param[FG_SETTING_DELTA_BSOC], 1, buf);
	rc = fg_set_param(chip, FG_SETTING_DELTA_BSOC, buf);
	if (rc) {
		dev_err(chip->dev, "failed to set delta_bsoc: %d\n", rc);
		return rc;
	}

	fg_encode(chip->param[FG_SETTING_SYS_TERM_CURRENT],
			chip->dt.sys_term_current_ma, buf);
	rc = fg_set_param(chip, FG_SETTING_SYS_TERM_CURRENT, buf);
	if (rc) {
		dev_err(chip->dev, "failed to write sys_term_current: %d\n", rc);
		return rc;
	}

	fg_encode(chip->param[FG_SETTING_CHG_TERM_CURRENT],
			chip->dt.chg_term_current_ma, buf);
	rc = fg_set_param(chip, FG_SETTING_CHG_TERM_BASE_CURRENT, buf);
	if (rc) {
		dev_err(chip->dev, "failed to write chg_term_current: %d\n", rc);
		return rc;
	}

	if (chip->batt_info.thermal_coeffs_len != 0) {
		rc = regmap_bulk_write(chip->regmap, BATT_INFO_THERM_C1(chip),
				chip->batt_info.thermal_coeffs,
				chip->batt_info.thermal_coeffs_len);
		if (rc < 0) {
			dev_err(chip->dev, "failed to write therm coeffs %d\n",
					rc);
			return rc;
		}
	}

	rc = fg_set_esr_timer(chip, chip->dt.esr_timer_charging[TIMER_RETRY],
			chip->dt.esr_timer_charging[TIMER_MAX], true);
	if (rc < 0) {
		dev_err(chip->dev, "failed to write ESR timer: %d\n", rc);
		return rc;
	}

	rc = fg_set_esr_timer(chip, chip->dt.esr_timer_awake[TIMER_RETRY],
			chip->dt.esr_timer_awake[TIMER_MAX], false);
	if (rc < 0) {
		dev_err(chip->dev, "failed to write ESR timer: %d\n", rc);
		return rc;
	}
	/*
	 * Set temperature limits
	 * Base is -30C, so we add 30C to the value
	 * Resolution is 0.5C, in deciCelcius
	 */
	buf[0] = DIV_ROUND_CLOSEST((chip->dt.cold_temp + 30) * 10, 5);
	regmap_write(chip->regmap, BATT_INFO_JEITA_COLD(chip), buf[0]);

	buf[0] = DIV_ROUND_CLOSEST((chip->dt.cool_temp + 30) * 10, 5);
	regmap_write(chip->regmap, BATT_INFO_JEITA_COOL(chip), buf[0]);

	buf[0] = DIV_ROUND_CLOSEST((chip->dt.warm_temp + 30) * 10, 5);
	regmap_write(chip->regmap, BATT_INFO_JEITA_WARM(chip), buf[0]);

	buf[0] = DIV_ROUND_CLOSEST((chip->dt.hot_temp + 30) * 10, 5);
	regmap_write(chip->regmap, BATT_INFO_JEITA_HOT(chip), buf[0]);

	if (chip->pmic_version == PMI8998_V2) {
		fg_encode(chip->param[FG_SETTING_RECHARGE_VOLT_THR],
				chip->dt.recharge_volt_thr_mv, buf);
		rc = fg_set_param(chip, FG_SETTING_RECHARGE_VOLT_THR, buf);
		if (rc) {
			dev_err(chip->dev, "failed to write recharge_volt_thr:"\
					" %d\n", rc);
			return rc;
		}

		fg_encode(chip->param[FG_SETTING_CHG_TERM_BASE_CURRENT],
				chip->dt.chg_term_base_current_ma, buf);
		rc = fg_set_param(chip, FG_SETTING_CHG_TERM_BASE_CURRENT, buf);
		if (rc) {
			dev_err(chip->dev, "failed to write term_base_curr: %d\n",
					rc);
			return rc;
		}
	}

	fg_encode(chip->param[FG_SETTING_ESR_TIGHT_FILTER],
			chip->dt.esr_tight_flt_upct, buf);
	rc = fg_set_param(chip, FG_SETTING_ESR_TIGHT_FILTER, buf);
	if (rc) {
		dev_err(chip->dev, "failed to write esr_tight_flt: %d\n", rc);
		return rc;
	}

	fg_encode(chip->param[FG_SETTING_ESR_BROAD_FILTER],
			chip->dt.esr_broad_flt_upct, buf);
	rc = fg_set_param(chip, FG_SETTING_ESR_BROAD_FILTER, buf);
	if (rc) {
		dev_err(chip->dev, "failed to write esr_broad_flt: %d\n", rc);
		return rc;
	}

	fg_encode(chip->param[FG_SETTING_ESR_PULSE_THR],
			chip->dt.esr_pulse_thresh_ma, buf);
	rc = fg_set_param(chip, FG_SETTING_ESR_PULSE_THR, buf);
	if (rc) {
		dev_err(chip->dev, "failed to write esr_pulse_thr: %d\n", rc);
		return rc;
	}

	get_esr_meas_current(chip->dt.esr_meas_curr_ma, buf);
	rc = regmap_update_bits(chip->regmap, BATT_INFO_ESR_PULL_DN_CFG(chip),
			ESR_PULL_DOWN_IVAL_MASK, buf[0]);
	if (rc) {
		dev_err(chip->dev, "failed to write esr_meas_curr_ma: %d\n", rc);
		return rc;
	}

	return 0;
}

static int fg_hw_init(struct fg_chip *chip)
{
	int rc;
	u8 buf[4];

	fg_encode(chip->param[FG_SETTING_TERM_CURRENT],
			chip->dt.term_current_ma, buf);
	rc = fg_set_param(chip, FG_SETTING_TERM_CURRENT, buf);
	if (rc) {
		dev_err(chip->dev, "failed to set term_current: %d\n", rc);
		return rc;
	}

	fg_encode(chip->param[FG_SETTING_CUTOFF_VOLT],
			chip->dt.cutoff_volt_mv, buf);
	rc = fg_set_param(chip, FG_SETTING_CUTOFF_VOLT, buf);
	if (rc) {
		dev_err(chip->dev, "failed to set cutoff_volt: %d\n", rc);
		return rc;
	}

	fg_encode(chip->param[FG_SETTING_BCL_LM_THR],
			chip->dt.bcl_lm_ma, buf);
	rc = fg_set_param(chip, FG_SETTING_BCL_LM_THR, buf);
	if (rc) {
		dev_err(chip->dev, "failed to set blc_lm: %d\n", rc);
		return rc;
	}

	fg_encode(chip->param[FG_SETTING_BCL_MH_THR],
			chip->dt.bcl_mh_ma, buf);
	rc = fg_set_param(chip, FG_SETTING_BCL_MH_THR, buf);
	if (rc) {
		dev_err(chip->dev, "failed to set blc_mh: %d\n", rc);
		return rc;
	}

	if (chip->dt.recharge_thr > 0) {
		fg_encode(chip->param[FG_SETTING_RECHARGE_THR],
				chip->dt.recharge_thr, buf);
		rc = fg_set_param(chip, FG_SETTING_RECHARGE_THR, buf);
		if (rc) {
			dev_err(chip->dev, "failed to set recharge_thr: %d\n", rc);
			return rc;
		}
	}

	fg_encode(chip->param[FG_SETTING_EMPTY_VOLT],
			chip->dt.empty_irq_volt_mv, buf);
	rc = fg_set_param(chip, FG_SETTING_EMPTY_VOLT,
			(u8 *)&chip->dt.empty_irq_volt_mv);
	if (rc) {
		dev_err(chip->dev, "failed to set empty_irq_volt: %d\n", rc);
		return rc;
	}

	/* Set delta_soc to 1, because its never any other value */
	fg_encode(chip->param[FG_SETTING_DELTA_MSOC], 1, buf);
	rc = fg_set_param(chip, FG_SETTING_DELTA_MSOC, buf);
	if (rc) {
		dev_err(chip->dev, "failed to set delta_msoc: %d\n", rc);
		return rc;
	}

	if (chip->dt.low_volt_thr_mv > 0) {
		fg_encode(chip->param[FG_SETTING_DELTA_MSOC],
				chip->dt.low_volt_thr_mv, buf);
		rc = fg_set_param(chip, FG_SETTING_BATT_LOW, buf);
		if (rc) {
			dev_err(chip->dev, "failed to write low_volt_thr: %d\n",
					rc);
			return rc;
		}
	}

	restore_cycle_counter(chip);

	switch (chip->pmic_version) {
		case PMI8950:
			rc = fg_hw_init_pmi8950(chip);
			rc |= fg_init_iadc_config(chip);
			return rc;
		case PMI8998_V1:
		case PMI8998_V2:
			return fg_hw_init_pmi8998(chip);
		default:
			return -EINVAL;
	}
}

#define PROFILE_LOAD_BIT		BIT(0)
#define BOOTLOADER_LOAD_BIT		BIT(1)
#define BOOTLOADER_RESTART_BIT		BIT(2)
#define HLOS_RESTART_BIT		BIT(3)
#define FG_PROFILE_LEN_PMI8950		128
#define FG_PROFILE_LEN_PMI8998		224
#define PROFILE_INTEGRITY_BIT		BIT(0)
#define PMI8950_BATT_PROFILE_REG	0x4c0
#define PMI8998_BATT_PROFILE_REG	24
static bool is_profile_load_required(struct fg_chip *chip)
{
	u8 buf[FG_PROFILE_LEN_PMI8998];
	u32 val;
	bool profiles_same = false;
	int rc;

	rc = fg_get_param(chip, FG_PROFILE_INTEGRITY, &val);
	if (rc < 0) {
		pr_err("failed to read profile integrity rc=%d\n", rc);
		return false;
	}

	/* Check if integrity bit is set */
	if (val & PROFILE_LOAD_BIT) {
		/* Whitelist the values */
		val &= ~PROFILE_LOAD_BIT;
		if (val != HLOS_RESTART_BIT && val != BOOTLOADER_LOAD_BIT &&
			val != (BOOTLOADER_LOAD_BIT | BOOTLOADER_RESTART_BIT) &&
			chip->pmic_version != PMI8950) {
			val |= PROFILE_LOAD_BIT;
			pr_warn("Garbage value in profile integrity word: 0x%x\n",
				val);
			return true;
		}

		switch (chip->pmic_version) {
		case PMI8950:
			rc = fg_sram_read(chip, buf, PMI8950_BATT_PROFILE_REG,
					128, 0, false);
			chip->batt_info.batt_profile_len = 128;
			break;
		case PMI8998_V1:
		case PMI8998_V2:
			rc = fg_sram_read(chip, buf, PMI8998_BATT_PROFILE_REG,
					148, 0, false);
			chip->batt_info.batt_profile_len = 148;
			break;
		}
		if (rc < 0) {
			pr_err("Error in reading battery profile, rc:%d\n", rc);
			return false;
		}
		profiles_same = memcmp(chip->batt_info.batt_profile, buf,
					32) == 0;
		if (profiles_same)
			return false;
	}
	return true;
}

#define LOW_LATENCY			BIT(6)
#define BATT_PROFILE_OFFSET		0x4C0
#define PROFILE_INTEGRITY_BIT		BIT(0)
#define FIRST_EST_DONE_BIT		BIT(5)
#define MAX_TRIES_FIRST_EST		3
#define FIRST_EST_WAIT_MS		2000
#define PROFILE_LOAD_TIMEOUT_MS		5000
static int fg_do_restart_pmi8950(struct fg_chip *chip, bool write_profile)
{
	int rc, ibat_ua;
	int reg = 0;
	bool tried_once = false;

	rc = fg_get_capacity(chip, &chip->last_cap);
	/*ignoring error*/

	chip->fg_restarting = true;
try_again:
	if (write_profile) {
		rc = fg_get_param(chip, FG_DATA_CURRENT, &ibat_ua);
		if (rc) {
			pr_err("failed to get current!\n");
			return rc;
		}

		if (ibat_ua < 0) {
			pr_warn("Charging enabled?, ibat_ua: %d\n", ibat_ua);

			if (!tried_once) {
				msleep(1000);
				tried_once = true;
				goto try_again;
			}
		}
	}

	/*
	 * release the sram access and configure the correct settings
	 * before re-requesting access.
	 */
	mutex_lock(&chip->sram_rw_lock);
	fg_release_access(chip);

	rc = regmap_update_bits(chip->regmap, SOC_BOOT_MODE_REG(chip),
			NO_OTP_PROF_RELOAD, 0);
	if (rc) {
		pr_err("failed to set no otp reload bit\n");
		goto unlock_and_fail;
	}

	/* unset the restart bits so the fg doesn't continuously restart */
	reg = REDO_FIRST_ESTIMATE | RESTART_GO;
	rc = regmap_update_bits(chip->regmap, SOC_RESTART_REG(chip),
			reg, 0);
	if (rc) {
		pr_err("failed to unset fg restart: %d\n", rc);
		goto unlock_and_fail;
	}

	rc = regmap_update_bits(chip->regmap, MEM_INTF_CFG(chip),
			LOW_LATENCY, LOW_LATENCY);
	if (rc) {
		pr_err("failed to set low latency access bit\n");
		goto unlock_and_fail;
	}
	mutex_unlock(&chip->sram_rw_lock);

	/* read once to get a fg cycle in */
	rc = fg_get_param(chip, FG_PROFILE_INTEGRITY, &rc);
	if (rc) {
		pr_err("failed to read profile integrity rc=%d\n", rc);
		goto fail;
	}

	/*
	 * If this is not the first time a profile has been loaded, sleep for
	 * 3 seconds to make sure the NO_OTP_RELOAD is cleared in memory
	 */
	if (chip->first_profile_loaded)
		msleep(3000);

	mutex_lock(&chip->sram_rw_lock);
	rc = regmap_update_bits(chip->regmap, MEM_INTF_CFG(chip), LOW_LATENCY,
			0);
	if (rc) {
		pr_err("failed to set low latency access bit\n");
		goto unlock_and_fail;
	}
	mutex_unlock(&chip->sram_rw_lock);

	if (write_profile) {
		/* write the battery profile */
		rc = fg_sram_write(chip, chip->batt_info.batt_profile,
				BATT_PROFILE_OFFSET,
				chip->batt_info.batt_profile_len, 0, true);
		if (rc) {
			pr_err("failed to write profile rc=%d\n", rc);
			goto fail;
		}
		/* write the integrity bits and release access */
		rc = fg_sram_masked_write(chip,
				chip->param[FG_PROFILE_INTEGRITY].address,
				PROFILE_INTEGRITY_BIT,
				PROFILE_INTEGRITY_BIT, 0);
		if (rc) {
			pr_err("failed to write profile rc=%d\n", rc);
			goto fail;
		}
	}

	/*
	 * make sure that the first estimate has completed
	 * in case of a hotswap
	 */
	rc = wait_for_completion_interruptible_timeout(&chip->first_soc_done,
			msecs_to_jiffies(PROFILE_LOAD_TIMEOUT_MS));
	if (rc <= 0) {
		pr_err("transaction timed out rc=%d\n", rc);
		rc = -ETIMEDOUT;
		goto fail;
	}

	/*
	 * reinitialize the completion so that the driver knows when the restart
	 * finishes
	 */
	reinit_completion(&chip->first_soc_done);

	/*
	 * set the restart bits so that the next fg cycle will not reload
	 * the profile
	 */
	rc = regmap_update_bits(chip->regmap, SOC_BOOT_MODE_REG(chip),
			NO_OTP_PROF_RELOAD, NO_OTP_PROF_RELOAD);
	if (rc) {
		pr_err("failed to set no otp reload bit\n");
		goto fail;
	}

	reg = REDO_FIRST_ESTIMATE | RESTART_GO;
	rc = regmap_update_bits(chip->regmap, SOC_RESTART_REG(chip),
			reg, reg);
	if (rc) {
		pr_err("failed to set fg restart: %d\n", rc);
		goto fail;
	}

	/* wait for the first estimate to complete */
	rc = wait_for_completion_interruptible_timeout(&chip->first_soc_done,
			msecs_to_jiffies(PROFILE_LOAD_TIMEOUT_MS));
	if (rc <= 0) {
		pr_err("transaction timed out rc=%d\n", rc);
		rc = -ETIMEDOUT;
		goto fail;
	}

	rc = regmap_read(chip->regmap, INT_RT_STS(chip->soc_base), &reg);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->soc_base), rc);
		goto fail;
	}
	if ((reg & FIRST_EST_DONE_BIT) == 0)
		pr_err("Battery profile reloading failed, no first estimate\n");

	rc = regmap_update_bits(chip->regmap, SOC_BOOT_MODE_REG(chip),
			NO_OTP_PROF_RELOAD, 0);
	if (rc) {
		pr_err("failed to set no otp reload bit\n");
		goto fail;
	}
	/* unset the restart bits so the fg doesn't continuously restart */
	rc = regmap_update_bits(chip->regmap, SOC_RESTART_REG(chip),
			REDO_FIRST_ESTIMATE | RESTART_GO, 0);
	if (rc) {
		pr_err("failed to unset fg restart: %d\n", rc);
		goto fail;
	}

	chip->fg_restarting = false;

	return 0;

unlock_and_fail:
	mutex_unlock(&chip->sram_rw_lock);
fail:
	chip->fg_restarting = false;
	return -EINVAL;
}

#define PROFILE_REG_PMI8998	24
#define PROFILE_OFFSET_PMI8998	0
#define SOC_READY_WAIT_MS	2000
static int fg_do_restart_pmi8998(struct fg_chip *chip, bool write_profile)
{
	int rc, msoc;
	u8 val;
	bool tried_again = false;

	rc = regmap_update_bits(chip->regmap, BATT_SOC_RESTART(chip),
			RESTART_GO, 0);
	if (rc) {
		dev_err(chip->dev, "failed to set restart bit: %d\n", rc);
		return -EINVAL;
	}

	/* load battery profile */
	rc = fg_sram_write(chip, chip->batt_info.batt_profile,
			PROFILE_REG_PMI8998, FG_PROFILE_LEN_PMI8998,
			PROFILE_OFFSET_PMI8998, false);
	if (rc) {
		pr_err("Error in writing battery profile, rc:%d\n", rc);
		goto out;
	}

	/* Set the profile integrity bit */
	val = HLOS_RESTART_BIT | PROFILE_LOAD_BIT;
	fg_set_param(chip, FG_PROFILE_INTEGRITY, &val);
	if (rc) {
		pr_err("failed to write profile integrity rc=%d\n", rc);
		goto out;
	}

	rc = fg_get_capacity(chip, &msoc);
	if (rc < 0) {
		dev_err(chip->dev, "failed to get capacity: %d\n", rc);
		return rc;
	}

	chip->last_cap = msoc;
	chip->fg_restarting = true;
	reinit_completion(&chip->first_soc_done);
	rc = regmap_update_bits(chip->regmap, BATT_SOC_RESTART(chip), RESTART_GO,
			RESTART_GO);
	if (rc < 0) {
		pr_err("Error in writing to %04x, rc=%d\n",
			BATT_SOC_RESTART(chip), rc);
		goto out;
	}

wait:
	rc = wait_for_completion_interruptible_timeout(&chip->first_soc_done,
		msecs_to_jiffies(SOC_READY_WAIT_MS));

	/* If we were interrupted wait again one more time. */
	if (rc == -ERESTARTSYS && !tried_again) {
		tried_again = true;
		goto wait;
	} else if (rc <= 0) {
		pr_err("wait for soc_ready timed out rc=%d\n", rc);
	}

	rc = regmap_update_bits(chip->regmap, BATT_SOC_RESTART(chip),
			RESTART_GO, 0);
	if (rc < 0) {
		dev_err(chip->dev, "failed to deassert restart: %d\n", rc);
		goto out;
	}
out:
	chip->fg_restarting = false;
	return rc;
}

#define PROFILE_COMPARE_LEN		32
#define ESR_MAX				300000
#define ESR_MIN				5000
static int fg_of_battery_profile_init(struct fg_chip *chip)
{
	struct device_node *batt_node;
	struct device_node *node = chip->dev->of_node;
	int rc = 0, len = 0;
	const char *data;

	batt_node = of_find_node_by_name(node, "qcom,battery-data");
	if (!batt_node) {
		dev_err(chip->dev, "No available batterydata\n");
		return rc;
	}

	chip->batt_info.manufacturer = of_get_property(batt_node,
			"manufacturer", &len);
	if (!chip->batt_info.manufacturer || !len)
		pr_warn("manufacturer not dound in dts");

	chip->batt_info.model = of_get_property(batt_node,
			"model", &len);
	if (!chip->batt_info.model || !len)
		pr_warn("model not dound in dts");

	chip->batt_info.serial_num = of_get_property(batt_node,
			"serial-number", &len);
	if (!chip->batt_info.manufacturer || !len)
		pr_warn("serial number not dound in dts");

	rc = of_property_read_u32(batt_node, "qcom,chg-rs-to-rslow",
					&chip->rslow_comp.chg_rs_to_rslow);
	if (rc) {
		dev_err(chip->dev, "Could not read rs to rslow: %d\n", rc);
		return -EINVAL;
	}

	rc = of_property_read_u32(batt_node, "qcom,chg-rslow-comp-c1",
					&chip->rslow_comp.chg_rslow_comp_c1);
	if (rc) {
		dev_err(chip->dev, "Could not read rslow comp c1: %d\n", rc);
		return -EINVAL;
	}
	rc = of_property_read_u32(batt_node, "qcom,chg-rslow-comp-c2",
					&chip->rslow_comp.chg_rslow_comp_c2);
	if (rc) {
		dev_err(chip->dev, "Could not read rslow comp c2: %d\n", rc);
		return -EINVAL;
	}
	rc = of_property_read_u32(batt_node, "qcom,chg-rslow-comp-thr",
					&chip->rslow_comp.chg_rslow_comp_thr);
	if (rc) {
		dev_err(chip->dev, "Could not read rslow comp thr: %d\n", rc);
		return -EINVAL;
	}

	rc = of_property_read_u32(batt_node, "qcom,fastchg-current-ma",
			&chip->batt_info.fastchg_curr_ma);
	if (rc < 0) {
		dev_err(chip->dev, "battery fastchg current unavailable");
		chip->batt_info.fastchg_curr_ma = -EINVAL;
	}

	rc = of_property_read_u32(batt_node, "qcom,max-voltage-uv",
					&chip->batt_info.batt_max_voltage_uv);

	if (rc)
		dev_warn(chip->dev, "couldn't find battery max voltage\n");

	data = of_get_property(chip->dev->of_node,
			"qcom,thermal-coefficients", &len);
	if (data && ((len == 6 && chip->pmic_version == PMI8950) ||
			(len == 3 && chip->pmic_version != PMI8950))) {
		memcpy(chip->batt_info.thermal_coeffs, data, len);
		chip->batt_info.thermal_coeffs_len = len;
	}

	data = of_get_property(batt_node, "qcom,fg-profile-data", &len);
	if (!data) {
		dev_err(chip->dev, "no battery profile loaded\n");
		return -EINVAL;
	}

	if ((chip->pmic_version == PMI8950 && len != FG_PROFILE_LEN_PMI8950) ||
		((chip->pmic_version == PMI8998_V1 ||
		chip->pmic_version == PMI8998_V2) &&
		len != FG_PROFILE_LEN_PMI8998)) {
		dev_err(chip->dev, "battery profile incorrect size: %d\n", len);
		return -EINVAL;
	}

	chip->batt_info.batt_profile = devm_kzalloc(chip->dev,
			sizeof(char) * len, GFP_KERNEL);

	if (!chip->batt_info.batt_profile) {
		dev_err(chip->dev, "coulnt't allocate mem for battery profile\n");
		rc = -ENOMEM;
		return rc;
	}

	return rc;
}

#define SW_CONFIG_OFFSET		0
static void fg_update_batt_profile(struct fg_chip *chip)
{
	int rc, offset, temp;
	u8 val;
	const struct fg_sram_param rslow_chg = chip->param[FG_SETTING_RSLOW_CHG],
		rslow_dischg = chip->param[FG_SETTING_RSLOW_DISCHG];

	rc = fg_sram_read(chip, &val, chip->param[FG_PROFILE_INTEGRITY].address,
			1, SW_CONFIG_OFFSET, false);
	if (rc < 0) {
		dev_err(chip->dev, "failed to get  SW_CONFIG: %d\n", rc);
		return;
	}

	/*
	 * If the RCONN had not been updated, no need to update battery
	 * profile. Else, update the battery profile so that the profile
	 * modified by bootloader or HLOS matches with the profile read
	 * from device tree.
	 */

	if (!(val & RCONN_CONFIG_BIT))
		return;

	rc = fg_get_param(chip, FG_SETTING_RSLOW_CHG, &temp);
	val = temp;
	if (rc) {
		pr_err("Error in reading ESR_RSLOW_CHG_OFFSET, rc=%d\n", rc);
		return;
	}
	offset = (rslow_chg.address - PROFILE_REG_PMI8998) * 4
			+ rslow_chg.offset;
	chip->batt_info.batt_profile[offset] = val;

	rc = fg_get_param(chip, FG_SETTING_RSLOW_DISCHG, &temp);
	val = temp;
	if (rc) {
		dev_err(chip->dev, "failed to get ESR_RSLOW_DISCHG: %d\n", rc);
		return;
	}
	offset = (rslow_dischg.address - PROFILE_REG_PMI8998) * 4
			+ rslow_dischg.offset;
	chip->batt_info.batt_profile[offset] = val;
}

static int fg_parse_ki_coefficients(struct fg_chip *chip)
{
	struct device_node *node = chip->dev->of_node;
	int rc, i, temp;

	rc = of_property_read_u32(node, "qcom,ki-coeff-full-dischg", &temp);
	if (!rc)
		chip->dt.ki_coeff_full_soc_dischg = temp;

	chip->dt.ki_coeff_hi_chg = -EINVAL;
	rc = of_property_read_u32(node, "qcom,ki-coeff-hi-chg", &temp);
	if (!rc)
		chip->dt.ki_coeff_hi_chg = temp;

	if (!of_find_property(node, "qcom,ki-coeff-soc-dischg", NULL) ||
		(!of_find_property(node, "qcom,ki-coeff-low-dischg", NULL) &&
		 !of_find_property(node, "qcom,ki-coeff-med-dischg", NULL) &&
		 !of_find_property(node, "qcom,ki-coeff-hi-dischg", NULL)))
		return 0;

	rc = of_property_read_u32_array(node, "qcom,ki-coeff-soc-dischg",
			chip->dt.ki_coeff_soc, KI_COEFF_SOC_LEVELS);
	if (rc < 0)
		return rc;

	rc = of_property_read_u32_array(node, "qcom,ki-coeff-low-dischg",
			chip->dt.ki_coeff_low_dischg, KI_COEFF_SOC_LEVELS);
	if (rc < 0)
		return rc;

	rc = of_property_read_u32_array(node, "qcom,ki-coeff-med-dischg",
			chip->dt.ki_coeff_med_dischg, KI_COEFF_SOC_LEVELS);
	if (rc < 0)
		return rc;

	rc = of_property_read_u32_array(node, "qcom,ki-coeff-hi-dischg",
			chip->dt.ki_coeff_hi_dischg, KI_COEFF_SOC_LEVELS);
	if (rc < 0)
		return rc;

	for (i = 0; i < KI_COEFF_SOC_LEVELS; i++) {
		if (chip->dt.ki_coeff_soc[i] < 0 ||
			chip->dt.ki_coeff_soc[i] > FULL_CAPACITY) {
			dev_err(chip->dev, "invalid ki_coeff_soc_dischg\n");
			return -EINVAL;
		}

		if (chip->dt.ki_coeff_low_dischg[i] < 0 ||
				chip->dt.ki_coeff_low_dischg[i] > KI_COEFF_MAX) {
			pr_err("invalid ki_coeff_low_dischg values\n");
			return -EINVAL;
		}

		if (chip->dt.ki_coeff_med_dischg[i] < 0 ||
				chip->dt.ki_coeff_med_dischg[i] > KI_COEFF_MAX) {
			pr_err("invalid ki_coeff_med_dischg values\n");
			return -EINVAL;
		}

		if (chip->dt.ki_coeff_hi_dischg[i] < 0 ||
				chip->dt.ki_coeff_hi_dischg[i] > KI_COEFF_MAX) {
			pr_err("invalid ki_coeff_hi_dischg values\n");
			return -EINVAL;
		}
	}
	chip->ki_coeff_dischg_en = true;
	return 0;
}

static int fg_of_init(struct fg_chip *chip)
{
	int rc;
	/* default values */
	switch (chip->pmic_version) {
	case PMI8950:
		chip->dt.term_current_ma = 250;
		chip->dt.chg_term_current_ma = 250;
		chip->dt.empty_irq_volt_mv = 3100;
		break;
	case PMI8998_V1:
		chip->dt.term_current_ma = 500;
		chip->dt.chg_term_current_ma = 100;
		chip->dt.sys_term_current_ma = -125;
		chip->dt.empty_irq_volt_mv = 2850;
		chip->dt.recharge_thr = 95;
		break;
	case PMI8998_V2:
		chip->dt.term_current_ma = 500;
		chip->dt.chg_term_current_ma = 100;
		chip->dt.chg_term_base_current_ma = 75;
		chip->dt.sys_term_current_ma = -125;
		chip->dt.empty_irq_volt_mv = 2850;
		chip->dt.recharge_volt_thr_mv = 4250;
		chip->dt.recharge_thr = 95;
		break;
	default:
		printk("invalid chip\n");
		return -EINVAL;
	}

	chip->dt.cutoff_volt_mv = 3200;
	chip->dt.bcl_lm_ma = 50;
	chip->dt.bcl_mh_ma = 752;
	chip->dt.cool_temp = 100;
	chip->dt.warm_temp = 400;
	chip->dt.cold_temp = 50;
	chip->dt.hot_temp = 450;
	chip->dt.esr_pulse_thresh_ma = 110;
	chip->dt.esr_tight_flt_upct = 3907;
	chip->dt.esr_broad_flt_upct = 99610;
	chip->dt.esr_meas_curr_ma = 120;
	chip->dt.esr_clamp_mohms = 20;
	chip->dt.esr_tight_lt_flt_upct = 30000;
	chip->dt.esr_broad_lt_flt_upct = 30000;
	chip->dt.esr_tight_rt_flt_upct = 5860;
	chip->dt.esr_broad_rt_flt_upct = 156250;
	chip->dt.esr_flt_switch_temp = 100;
	chip->cl.max_temp = 450;
	chip->cl.min_temp = 150;
	chip->cl.max_start_soc = 15;
	chip->cl.max_cap_inc = 5;
	chip->cl.max_cap_dec = 100;
	chip->cl.vbat_est_thr_uv = 40000;

	of_property_read_u32(chip->dev->of_node,
		"qcom,term-current-ma", &chip->dt.term_current_ma);

	of_property_read_u32(chip->dev->of_node,
		"qcom,chg-term-base-current-ma", &chip->dt.chg_term_base_current_ma);

	of_property_read_u32(chip->dev->of_node,
		"qcom,chg-term-current-ma", &chip->dt.term_current_ma);

	of_property_read_u32(chip->dev->of_node,
		"qcom,cutoff-volt-mv", &chip->dt.cutoff_volt_mv);

	of_property_read_u32(chip->dev->of_node,
		"qcom,const-charge-volt-mv", &chip->dt.const_charge_volt_mv);

	of_property_read_u32(chip->dev->of_node,
		"qcom,recharge-thr", &chip->dt.recharge_thr);

	of_property_read_u32(chip->dev->of_node,
		"qcom,recharge-volt-thr-mv", &chip->dt.recharge_volt_thr_mv);

	of_property_read_u32(chip->dev->of_node,
		"qcom,bcl-lm-thr-ma", &chip->dt.bcl_lm_ma);

	of_property_read_u32(chip->dev->of_node,
		"qcom,bcl-mh-thr-ma", &chip->dt.bcl_mh_ma);

	of_property_read_u32(chip->dev->of_node,
		"qcom,cool-temp-c", &chip->dt.cool_temp);

	of_property_read_u32(chip->dev->of_node,
		"qcom,warm-temp-c", &chip->dt.warm_temp);

	of_property_read_u32(chip->dev->of_node,
		"qcom,cold-temp-c", &chip->dt.cold_temp);

	of_property_read_u32(chip->dev->of_node,
		"qcom,hot-temp-c", &chip->dt.hot_temp);

	of_property_read_u32(chip->dev->of_node,
		"qcom,empty-irq-volt-mv", &chip->dt.empty_irq_volt_mv);

	of_property_read_u32(chip->dev->of_node,
		"qcom,low-volt-thr-mv", &chip->dt.low_volt_thr_mv);

	of_property_read_u32(chip->dev->of_node,
		"qcom,vbat-estimate-diff-mv", &chip->dt.vbatt_est_diff);

	of_property_read_u32_array(chip->dev->of_node,
		"qcom,fg-esr-timer-charging",
		chip->dt.esr_timer_charging, NUM_ESR_TIMERS);

	of_property_read_u32_array(chip->dev->of_node,
		"qcom,fg-esr-timer-awake",
		chip->dt.esr_timer_awake, NUM_ESR_TIMERS);

	of_property_read_u32_array(chip->dev->of_node,
		"qcom,fg-esr-timer-asleep",
		chip->dt.esr_timer_asleep, NUM_ESR_TIMERS);

	of_property_read_u32_array(chip->dev->of_node,
		"qcom,fg-esr-timer-shutdown",
		chip->dt.esr_timer_shutdown, NUM_ESR_TIMERS);

	chip->reset_on_lockup = of_property_read_bool(chip->dev->of_node,
			"qcom,fg-reset-on-lockup");

	rc = of_property_read_u32(chip->dev->of_node, "qcom,fg-rconn-mohm",
			&chip->batt_info.rconn_mohm);
	if (rc)
		chip->batt_info.rconn_mohm = 1;

	rc = fg_parse_ki_coefficients(chip);
	if (rc)
		dev_err(chip->dev, "failed to parse ki coeffs: %d\n", rc);

	return 0;
}

//TODO: clean these up
#define DMA_WRITE_ERROR_BIT			BIT(1)
#define DMA_READ_ERROR_BIT			BIT(2)
#define DMA_CLEAR_LOG_BIT			BIT(0)
int fg_check_and_clear_dma_errors(struct fg_chip *chip)
{
	int rc;
	u8 dma_sts;
	bool error_present;

	//TODO: cleanup DMA sts reads
	rc = fg_read(chip, &dma_sts, chip->mem_base + 0x70, 1);
	if (rc < 0) {
		pr_err("failed to dma_sts, rc=%d\n", rc);
		return rc;
	}

	error_present = dma_sts & (DMA_WRITE_ERROR_BIT | DMA_READ_ERROR_BIT);
	rc = fg_masked_write(chip, chip->mem_base + 0x71, DMA_CLEAR_LOG_BIT,
			error_present ? DMA_CLEAR_LOG_BIT : 0);
	if (rc < 0) {
		pr_err("failed to write dma_ctl, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static void fg_rslow_update(struct fg_chip *chip)
{
#if 0
	int battery_soc_1b;

	int rc = fg_get_param(chip, FG_DATA_BATT_SOC, &battery_soc_1b);
	if (rc) {
		dev_err(chip->dev, "failed to get batt_soc: %d\n", rc);
		return;
	}

	/*batt_soc is 3 bytes, and we only need the first byte*/
	battery_soc_1b >>= 16;

	if (battery_soc_1b > chip->rslow_comp.chg_rslow_comp_thr
			&& chip->status == POWER_SUPPLY_STATUS_CHARGING) {
		if (!chip->rslow_comp.active)
			fg_rslow_charge_comp_set(chip);
	} else {
		if (chip->rslow_comp.active)
			fg_rslow_charge_comp_clear(chip);
	}
#endif
}

static bool fg_is_temperature_ok_for_learning(struct fg_chip *chip)
{
	int batt_temp;
	fg_get_param(chip, FG_DATA_BATT_TEMP, &batt_temp);

	if (batt_temp > chip->cl.max_temp
			|| batt_temp < chip->cl.min_temp)
		return false;

	return true;
}

#define PMI8950_MAH_TO_SOC_CONV_REG	0x4a0
#define CC_SOC_COEFF_OFFSET		0
static int fg_calc_and_store_cc_soc_coeff(struct fg_chip *chip, int16_t cc_mah)
{
	int rc;
	int cc_to_soc_coeff, mah_to_soc;
	u8 buf[4];

	fg_encode(chip->param[FG_PARAM_ACTUAL_CAP], cc_mah, buf);
	rc = fg_set_param(chip, FG_PARAM_ACTUAL_CAP, buf);
	if (rc) {
		pr_err("Failed to store actual capacity: %d\n", rc);
		return rc;
	}

	rc = fg_sram_read(chip, (u8 *)& mah_to_soc, PMI8950_MAH_TO_SOC_CONV_REG,
			2, 0, false);
	if (rc) {
		pr_err("Failed to read mah_to_soc_conv_cs: %d\n", rc);
	} else {
		cc_to_soc_coeff = div64_s64(mah_to_soc, cc_mah);
		fg_encode(chip->param[FG_PARAM_ACTUAL_CAP], cc_to_soc_coeff,
				buf);
		rc = fg_set_param(chip, FG_PARAM_ACTUAL_CAP, buf);
		if (rc)
			pr_err("Failed to write cc_soc_coeff_offset: %d\n",
				rc);
	}
	return rc;
}

#define ACT_BATT_CAP_REG	74
#define ACT_BATT_CAP_LEN	2
#define ACT_BATT_CAP_OFF	0
static int fg_save_learned_cap_to_sram(struct fg_chip *chip)
{
	int16_t cc_mah;
	int rc;

	if (!chip->cl.learned_cc_uah)
		return -EPERM;

	cc_mah = div64_s64(chip->cl.learned_cc_uah, 1000);
	/* Write to actual capacity register for coulomb counter operation */
	rc = fg_set_param(chip, FG_DATA_ACT_CAP, (u8 *)&cc_mah);
	if (rc < 0) {
		dev_err(chip->dev, "failed to write act_batt_cap: %d\n", rc);
		return rc;
	}

	switch (chip->pmic_version) {
	case PMI8950:
		/* store charge coefficients */
		rc = fg_calc_and_store_cc_soc_coeff(chip, cc_mah);
		break;
	case PMI8998_V1:
	case PMI8998_V2:
		/* Write to a backup register to use across reboot */
		rc = fg_sram_write(chip, (u8 *)&cc_mah, ACT_BATT_CAP_REG,
				ACT_BATT_CAP_LEN, ACT_BATT_CAP_OFF, false);
	}

	if (rc)
		dev_err(chip->dev, "failed to write learned cap: %d\n", rc);

	return rc;
}

#define CAPACITY_DELTA_DECIPCT	500
static int fg_load_learned_cap_from_sram(struct fg_chip *chip)
{
	int rc, act_cap_mah;
	int64_t delta_cc_uah, pct_nom_cap_uah;

	rc = fg_get_param(chip, FG_DATA_ACT_CAP, &act_cap_mah);
	if (rc < 0) {
		pr_err("Error in getting ACT_BATT_CAP, rc=%d\n", rc);
		return rc;
	}

	chip->cl.learned_cc_uah = act_cap_mah * 1000;

	if (chip->cl.learned_cc_uah != chip->cl.nom_cap_uah) {
		if (chip->cl.learned_cc_uah == 0)
			chip->cl.learned_cc_uah = chip->cl.nom_cap_uah;

		delta_cc_uah = abs(chip->cl.learned_cc_uah -
					chip->cl.nom_cap_uah);
		pct_nom_cap_uah = div64_s64((int64_t)chip->cl.nom_cap_uah *
				CAPACITY_DELTA_DECIPCT, 1000);
		/*
		 * If the learned capacity is out of range by 50% from the
		 * nominal capacity, then overwrite the learned capacity with
		 * the nominal capacity.
		 */
		if (chip->cl.nom_cap_uah && delta_cc_uah > pct_nom_cap_uah)
			chip->cl.learned_cc_uah = chip->cl.nom_cap_uah;

		rc = fg_save_learned_cap_to_sram(chip);
		if (rc < 0)
			pr_err("Error in saving learned_cc_uah, rc=%d\n", rc);
	}

	return 0;
}

#define FULL_CAPACITY	100
#define FULL_SOC_RAW	255
#define BATT_SOC_32BIT	GENMASK(31, 0)
#define CC_SOC_30BIT	GENMASK(29, 0)
#define CC_SOC_30BIT	GENMASK(29, 0)
static int fg_cap_learning_begin(struct fg_chip *chip, u32 batt_soc)
{
	int rc, cc_soc_sw, batt_soc_msb;

	batt_soc_msb = batt_soc >> 24;
	if (DIV_ROUND_CLOSEST(batt_soc_msb * 100, FULL_SOC_RAW) >
		chip->cl.max_start_soc) {
		return -EINVAL;
	}

	chip->cl.init_cc_uah = div64_s64(chip->cl.learned_cc_uah * batt_soc_msb,
					FULL_SOC_RAW);

	/* Prime cc_soc_sw with battery SOC when capacity learning begins */
	cc_soc_sw = div64_s64((int64_t)batt_soc * CC_SOC_30BIT,
				BATT_SOC_32BIT);
	rc = fg_set_param(chip, FG_DATA_CHARGE_COUNTER, (u8 *)&cc_soc_sw);
	if (rc) {
		dev_err(chip->dev, "failed to write cc_soc_sw: %d\n", rc);
		return rc;
	}

	chip->cl.init_cc_soc_sw = cc_soc_sw;
	return rc;
}

static int fg_cap_learning_process_full_data(struct fg_chip *chip)
{
	int rc, cc_soc_sw, cc_soc_delta_pct;
	int64_t delta_cc_uah;

	rc = fg_get_param(chip, FG_DATA_CHARGE_COUNTER, &cc_soc_sw);
	if (rc < 0) {
		dev_err(chip->dev, "failed to get CC_SOC_SW: %d\n", rc);
		return rc;
	}

	cc_soc_delta_pct =
		div64_s64((int64_t)(cc_soc_sw - chip->cl.init_cc_soc_sw) * 100,
			CC_SOC_30BIT);

	/* If the delta is < 50%, then skip processing full data */
	if (cc_soc_delta_pct < 50) {
		dev_err(chip->dev, "cc_soc_delta_pct: %d\n", cc_soc_delta_pct);
		return -ERANGE;
	}

	delta_cc_uah = div64_s64(chip->cl.learned_cc_uah * cc_soc_delta_pct,
				100);
	chip->cl.final_cc_uah = chip->cl.init_cc_uah + delta_cc_uah;
	return 0;
}

static void fg_cap_learning_post_process(struct fg_chip *chip)
{
	int64_t max_inc_val, min_dec_val, old_cap;
	int rc;

	if (chip->pmic_version != PMI8950)
		dev_warn(chip->dev, "QNOVO is not supported\n");

	max_inc_val = chip->cl.learned_cc_uah
			* (1000 + chip->cl.max_cap_inc);
	do_div(max_inc_val, 1000);

	min_dec_val = chip->cl.learned_cc_uah
			* (1000 - chip->cl.max_cap_dec);
	do_div(min_dec_val, 1000);

	old_cap = chip->cl.learned_cc_uah;
	if (chip->cl.final_cc_uah > max_inc_val)
		chip->cl.learned_cc_uah = max_inc_val;
	else if (chip->cl.final_cc_uah < min_dec_val)
		chip->cl.learned_cc_uah = min_dec_val;
	else
		chip->cl.learned_cc_uah =
			chip->cl.final_cc_uah;

	if (chip->cl.max_cap_limit) {
		max_inc_val = (int64_t)chip->cl.nom_cap_uah * (1000 +
				chip->cl.max_cap_limit);
		do_div(max_inc_val, 1000);
		if (chip->cl.final_cc_uah > max_inc_val)
			chip->cl.learned_cc_uah = max_inc_val;
	}

	if (chip->cl.min_cap_limit) {
		min_dec_val = (int64_t)chip->cl.nom_cap_uah * (1000 -
				chip->cl.min_cap_limit);
		do_div(min_dec_val, 1000);
		if (chip->cl.final_cc_uah < min_dec_val)
			chip->cl.learned_cc_uah = min_dec_val;
	}

	rc = fg_save_learned_cap_to_sram(chip);
	if (rc < 0)
		pr_err("Error in saving learned_cc_uah, rc=%d\n", rc);
}

static int fg_cap_learning_done(struct fg_chip *chip)
{
	int rc, cc_soc_sw;

	rc = fg_cap_learning_process_full_data(chip);
	if (rc < 0) {
		dev_err(chip->dev, "failed to process cap learning data: %d\n",
			rc);
		return rc;
	}

	/* Write a FULL value to cc_soc_sw */
	cc_soc_sw = CC_SOC_30BIT;
	rc = fg_set_param(chip, FG_DATA_CHARGE_COUNTER, (u8 *)&cc_soc_sw);
	if (rc) {
		dev_err(chip->dev, "failed to write cc_soc_sw: %d\n", rc);
		return rc;
	}

	fg_cap_learning_post_process(chip);
	return rc;
}

static void fg_cap_learning_update(struct fg_chip *chip)
{
	int rc, batt_soc, batt_soc_msb, cc_soc_sw;
	bool input_present = is_input_present(chip);
	bool prime_cc = false;

	mutex_lock(&chip->cl.lock);

	if (!fg_is_temperature_ok_for_learning(chip) ||
			!chip->cl.learned_cc_uah) {
		chip->cl.active = false;
		chip->cl.init_cc_uah = 0;
		goto out;
	}

	if (chip->charge_status == chip->prev_charge_status)
		goto out;

	rc = fg_get_param(chip, FG_DATA_BATT_SOC, &batt_soc);
	if (rc < 0) {
		pr_err("Error in getting ACT_BATT_CAP, rc=%d\n", rc);
		goto out;
	}

	batt_soc_msb = (u32)batt_soc >> 24;

	/* Initialize the starting point of learning capacity */
	if (!chip->cl.active) {
		if (chip->charge_status == POWER_SUPPLY_STATUS_CHARGING) {
			rc = fg_cap_learning_begin(chip, batt_soc);
			chip->cl.active = (rc == 0);
		} else {
			if ((chip->charge_status ==
					POWER_SUPPLY_STATUS_DISCHARGING) ||
					chip->charge_done)
				prime_cc = true;
		}
	} else {
		if (chip->charge_done) {
			rc = fg_cap_learning_done(chip);
			if (rc < 0)
				dev_err(chip->dev, "failed to complete"\
						" capacity learning: %d\n", rc);

			chip->cl.active = false;
			chip->cl.init_cc_uah = 0;
		}

		if (chip->charge_status == POWER_SUPPLY_STATUS_DISCHARGING) {
			if (!input_present) {
				chip->cl.active = false;
				chip->cl.init_cc_uah = 0;
				prime_cc = true;
			}
		}

		if (chip->charge_status == POWER_SUPPLY_STATUS_NOT_CHARGING) {
			if (input_present) {
				/*
				 * Don't abort the capacity learning when qnovo
				 * is enabled and input is present where the
				 * charging status can go to "not charging"
				 * intermittently.
				 * TODO:
				 * check for QNOVO
				 */
			} else {
				chip->cl.active = false;
				chip->cl.init_cc_uah = 0;
				prime_cc = true;
			}
		}
	}

	/*
	 * Prime CC_SOC_SW when the device is not charging or during charge
	 * termination when the capacity learning is not active.
	 */

	if (prime_cc) {
		if (chip->charge_done)
			cc_soc_sw = CC_SOC_30BIT;
		else
			cc_soc_sw = div_u64((u32)batt_soc *
					CC_SOC_30BIT, BATT_SOC_32BIT);

		rc = fg_set_param(chip, FG_DATA_CHARGE_COUNTER, (u8 *)&cc_soc_sw);
		if (rc < 0)
			dev_err(chip->dev, "failed to write cc_soc_sw: %d\n", rc);
	}

out:
	mutex_unlock(&chip->cl.lock);
}

static void fg_battery_profile_load_work(struct work_struct *work)
{
	struct fg_chip *chip = container_of(work,
			struct fg_chip,
			profile_load_work.work);
	int rc;

	/*TODO disable charging when this starts*/
	rc = fg_of_battery_profile_init(chip);
	if (rc) {
		dev_err(chip->dev, "failed to get profile from dt: %d\n", rc);
		goto out;
	}

	if (chip->pmic_version != PMI8950)
		fg_update_batt_profile(chip);

	if (!is_profile_load_required(chip))
		goto done;

	dev_warn(chip->dev, "profile load requested!\n");
	clear_cycle_counter(chip);
	mutex_lock(&chip->cl.lock);
	chip->cl.learned_cc_uah = 0;
	chip->cl.active = false;
	mutex_unlock(&chip->cl.lock);

	chip->do_restart(chip, true);

done:
	rc = fg_get_param(chip, FG_DATA_NOM_CAP, &chip->cl.nom_cap_uah);
	if (rc) {
		dev_err(chip->dev, "failed to get nom_cap: %d\n", rc);
	} else {
		rc = fg_load_learned_cap_from_sram(chip);
		if (rc)
			dev_err(chip->dev, "failed to load capacity: %d\n", rc);
	}

	rc = chip->rconn_config(chip);
	if (rc < 0)
		pr_err("Error in configuring Rconn, rc=%d\n", rc);

	batt_psy_initialized(chip);
	fg_notify_charger(chip);

out:
	chip->first_profile_loaded = true;
	chip->soc_reporting_ready = true;
	if (!work_pending(&chip->status_change_work)) {
		//fg_stay_awake(chip, FG_STATUS_NOTIFY_WAKE);
		schedule_work(&chip->status_change_work);
	}
}

static int __fg_esr_filter_config(struct fg_chip *chip,
				enum esr_filter_status esr_flt_sts)
{
	u8 esr_tight_flt, esr_broad_flt;
	int esr_tight_flt_upct, esr_broad_flt_upct;
	int rc;

	if (esr_flt_sts == chip->esr_flt_sts)
		return 0;

	switch (esr_flt_sts) {
	case ROOM_TEMP:
		esr_tight_flt_upct = chip->dt.esr_tight_flt_upct;
		esr_broad_flt_upct = chip->dt.esr_broad_flt_upct;
		break;
	case LOW_TEMP:
		esr_tight_flt_upct = chip->dt.esr_tight_lt_flt_upct;
		esr_broad_flt_upct = chip->dt.esr_broad_lt_flt_upct;
		break;
	case RELAX_TEMP:
		esr_tight_flt_upct = chip->dt.esr_tight_rt_flt_upct;
		esr_broad_flt_upct = chip->dt.esr_broad_rt_flt_upct;
		break;
	default:
		dev_err(chip->dev, "unknown esr_flt_sts: %d\n", esr_flt_sts);
		return 0;
	}

	fg_encode(chip->param[FG_SETTING_ESR_TIGHT_FILTER], esr_tight_flt_upct,
		&esr_tight_flt);
	rc = fg_set_param(chip, FG_SETTING_ESR_TIGHT_FILTER, &esr_tight_flt);
	if (rc < 0) {
		dev_err(chip->dev, "failed to write ESR LT tight filter: %d\n",
				rc);
		return rc;
	}

	fg_encode(chip->param[FG_SETTING_ESR_BROAD_FILTER], esr_broad_flt_upct,
		&esr_broad_flt);
	rc = fg_set_param(chip, FG_SETTING_ESR_BROAD_FILTER, &esr_broad_flt);
	if (rc < 0) {
		pr_err("Error in writing ESR LT broad filter, rc=%d\n", rc);
		return rc;
	}

	chip->esr_flt_sts = esr_flt_sts;
	return 0;
}

#define DT_IRQ_COUNT			3
#define DELTA_TEMP_IRQ_TIME_MS		300000
#define ESR_FILTER_ALARM_TIME_MS	900000
static int fg_esr_filter_config(struct fg_chip *chip, int batt_temp,
				bool override)
{
	enum esr_filter_status esr_flt_sts = ROOM_TEMP;
	bool input_present, count_temp_irq = false;
	s64 time_ms;
	int rc;

	/*
	 * If the battery temperature is lower than -20 C, then skip modifying
	 * ESR filter.
	 */
	if (batt_temp < -210)
		return 0;

	input_present = is_input_present(chip);

	/* TODO:Qnovo isn't supported on mainline */

	/*
	 * If battery temperature is lesser than 10 C (default), then apply the
	 * ESR low temperature tight and broad filter values to ESR room
	 * temperature tight and broad filters. If battery temperature is higher
	 * than 10 C, then apply back the room temperature ESR filter
	 * coefficients to ESR room temperature tight and broad filters.
	 */
	if (batt_temp > chip->dt.esr_flt_switch_temp)
		esr_flt_sts = ROOM_TEMP;
	else
		esr_flt_sts = LOW_TEMP;

	if (count_temp_irq) {
		time_ms = ktime_ms_delta(ktime_get(),
				chip->last_delta_temp_time);
		chip->delta_temp_irq_count++;

		if (chip->delta_temp_irq_count >= DT_IRQ_COUNT
			&& time_ms <= DELTA_TEMP_IRQ_TIME_MS) {
			esr_flt_sts = RELAX_TEMP;
		}
	}

	rc = __fg_esr_filter_config(chip, esr_flt_sts);
	if (rc < 0)
		return rc;

	if (esr_flt_sts == RELAX_TEMP)
		alarm_start_relative(&chip->esr_filter_alarm,
			ms_to_ktime(ESR_FILTER_ALARM_TIME_MS));

	return 0;
}

static int fg_esr_validate(struct fg_chip *chip)
{
	int rc, esr_uohms;
	u8 buf[2];

	if (chip->dt.esr_clamp_mohms <= 0)
		return 0;

	rc = fg_get_param(chip, FG_DATA_BATT_ESR, &esr_uohms);
	if (rc < 0) {
		dev_err(chip->dev, "failed to get ESR: %d\n", rc);
		return rc;
	}

	if (esr_uohms >= chip->dt.esr_clamp_mohms * 1000)
		return 0;

	esr_uohms = chip->dt.esr_clamp_mohms * 1000;
	fg_encode(chip->param[FG_DATA_BATT_ESR], esr_uohms, buf);
	rc = fg_set_param(chip, FG_DATA_BATT_ESR, buf);
	if (rc < 0)
		dev_err(chip->dev, "failed to set ESR: %d\n", rc);

	return rc;
}

#define MAX_BATTERY_CC_SOC_CAPACITY		150
static void status_change_work_pmi8950(struct work_struct *work)
{
	struct fg_chip *chip = container_of(work,
				struct fg_chip,
				status_change_work);
	int batt_soc, rc, capacity;
	bool batt_missing = is_battery_missing(chip);
	bool input_present = is_input_present(chip);
	bool otg_present = is_otg_present(chip);

	if (batt_missing)
		return;

	rc = fg_get_capacity(chip, &capacity);

	if (chip->status == POWER_SUPPLY_STATUS_FULL) {
		/* NOTE: hold-soc-while-full is assumed */
		if (capacity >= 99 && chip->health == POWER_SUPPLY_HEALTH_GOOD)
			chip->charge_full = true;
	}
	if (chip->status == POWER_SUPPLY_STATUS_FULL ||
			chip->status == POWER_SUPPLY_STATUS_CHARGING) {
		if (!chip->vbat_low_irq_enabled &&
				!chip->use_vbat_low_empty_soc) {
			enable_irq(chip->irqs[VBATT_LOW_8950_IRQ].irq);
			enable_irq_wake(chip->irqs[VBATT_LOW_8950_IRQ].irq);
			chip->vbat_low_irq_enabled = true;
		}

		if (!chip->full_soc_irq_enabled) {
			enable_irq(chip->irqs[FULL_SOC_IRQ].irq);
			enable_irq_wake(chip->irqs[FULL_SOC_IRQ].irq);
			chip->full_soc_irq_enabled = true;
		}
	} else if (chip->status == POWER_SUPPLY_STATUS_DISCHARGING) {
		if (chip->vbat_low_irq_enabled &&
				!chip->use_vbat_low_empty_soc) {
			disable_irq_wake(chip->irqs[VBATT_LOW_8950_IRQ].irq);
			disable_irq_nosync(chip->irqs[VBATT_LOW_8950_IRQ].irq);
			chip->vbat_low_irq_enabled = false;
		}

		if (chip->full_soc_irq_enabled) {
			disable_irq_wake(chip->irqs[FULL_SOC_IRQ].irq);
			disable_irq_nosync(chip->irqs[FULL_SOC_IRQ].irq);
			chip->full_soc_irq_enabled = false;
		}
	}
	fg_cap_learning_update(chip);
	fg_update_esr_values(chip);

	if (chip->prev_status != chip->status) {
		/*
		 * Reset SW_CC_SOC to a value based off battery SOC when
		 * the device is discharging.
		 */
		if (chip->status == POWER_SUPPLY_STATUS_DISCHARGING) {
			const struct fg_sram_param batt_soc_p =
				chip->param[FG_DATA_BATT_SOC];
			rc = fg_sram_read(chip, (u8 *)&batt_soc,
					batt_soc_p.address, batt_soc_p.length,
					batt_soc_p.offset, false);
			if (rc)
				return;
			if (!batt_soc)
				return;

			batt_soc = div64_s64((int64_t)batt_soc *
					FULL_PERCENT_28BIT, FULL_PERCENT_3B);
			rc = fg_set_param(chip, FG_DATA_CHARGE_COUNTER,
					(u8 *)&batt_soc);
			if (rc)
				dev_err(chip->dev,
					"failed to reset CC_SOC_REG: %d\n", rc);
		}

		fg_cycle_counter_update(chip);
	}

	if (chip->input_present ^ input_present ||
		chip->otg_present ^ otg_present) {
		//TODO:fg_stay_awake(chip, FG_GAIN_COMP_WAKE);
		chip->input_present = input_present;
		chip->otg_present = otg_present;
		fg_iadc_gain_comp(chip);
	}
}

static void status_change_work_pmi8998(struct work_struct *work)
{
	struct fg_chip *chip = container_of(work,
			struct fg_chip, status_change_work);
	union power_supply_propval prop = {0, };
	int rc, batt_temp;

	if (!batt_psy_initialized(chip))
		goto out;

	if (!chip->soc_reporting_ready)
		goto out;

	rc = power_supply_get_property(chip->batt_psy, POWER_SUPPLY_PROP_STATUS,
			&prop);
	if (rc < 0) {
		pr_err("Error in getting charging status, rc=%d\n", rc);
		goto out;
	}

	chip->charge_status = prop.intval;

	/*TODO: investigate
	rc = power_supply_get_property(chip->batt_psy,
			POWER_SUPPLY_PROP_CHARGE_DONE, &prop);
	if (rc < 0) {
		pr_err("Error in getting charge_done, rc=%d\n", rc);
		goto out;
	}
	chip->charge_done = prop.intval;*/
	fg_cycle_counter_update(chip);
	fg_cap_learning_update(chip);

	rc = fg_charge_full_update(chip);
	if (rc < 0)
		pr_err("Error in charge_full_update, rc=%d\n", rc);

	/* NOTE: auto-recharge-soc is assumed */
	rc = fg_adjust_ki_coeff_dischg(chip);
	if (rc < 0)
		pr_err("Error in adjusting ki_coeff_dischg, rc=%d\n", rc);

	rc = fg_esr_fcc_config(chip);
	if (rc < 0)
		pr_err("Error in adjusting FCC for ESR, rc=%d\n", rc);

	rc = fg_get_temperature(chip, &batt_temp);
	if (!rc) {
		rc = fg_adjust_ki_coeff_full_soc(chip, batt_temp);
		if (rc < 0)
			pr_err("Error in configuring ki_coeff_full_soc rc:%d\n",
				rc);
	}

	chip->prev_charge_status = chip->charge_status;
out:
	return;
	//TODO:fg_relax(chip, FG_STATUS_NOTIFY_WAKE);
}

#define IACS_INTR_SRC_SLCT	BIT(3)
static int fg_init_memif(struct fg_chip *chip)
{
	enum dig_rev_offset {
		DIG_MINOR = 0x0,
		DIG_MAJOR = 0x1,
		ANA_MINOR = 0x2,
		ANA_MAJOR = 0x3,
	};
	enum dig_major {
		DIG_REV_1 = 0x1,
		DIG_REV_2 = 0x2,
		DIG_REV_3 = 0x3,
	};
	int rc;

	rc = fg_read(chip, chip->revision, chip->mem_base + DIG_MINOR, 4);
	if (rc) {
		dev_err(chip->dev, "Unable to read FG revision rc=%d\n", rc);
		return rc;
	}

	switch (chip->revision[DIG_MAJOR]) {
	case DIG_REV_1:
	case DIG_REV_2:
		break;
	case DIG_REV_3:
		chip->ima_supported = true;
		break;
	default:
		pr_err("Digital Major rev=%d not supported\n",
				chip->revision[DIG_MAJOR]);
		return -EINVAL;
	}

	if (chip->pmic_version == PMI8998_V1 || chip->pmic_version == PMI8998_V2)
		chip->ima_supported = true;

	pr_info("FG Probe - FG Revision DIG:%d.%d ANA:%d.%d PMIC subtype=%d\n",
		chip->revision[DIG_MAJOR], chip->revision[DIG_MINOR],
		chip->revision[ANA_MAJOR], chip->revision[ANA_MINOR],
		chip->pmic_version);

	if (chip->ima_supported) {
		/*
		 * Change the FG_MEM_INT interrupt to track IACS_READY
		 * condition instead of end-of-transaction. This makes sure
		 * that the next transaction starts only after the hw is ready.
		 */
		rc = fg_masked_write(chip,
			MEM_INTF_IMA_CFG(chip), IACS_INTR_SRC_SLCT,
			IACS_INTR_SRC_SLCT);
		if (rc) {
			dev_err(chip->dev,
				"failed to configure interrupt source %d\n",
				rc);
			return rc;
		}

		/* check for error condition */
		rc = fg_check_and_clear_ima_errors(chip, true);
		if (rc && rc != -EAGAIN) {
			pr_err("Error in clearing IMA exception rc=%d", rc);
			return rc;
		}
	}

	if (chip->pmic_version == PMI8998_V1 ||
		chip->pmic_version == PMI8998_V2)
		fg_check_and_clear_dma_errors(chip);

	return 0;
}

static enum power_supply_property fg_properties[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_STATUS,
};

static int fg_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct fg_chip *chip = power_supply_get_drvdata(psy);
	int error = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = chip->batt_info.manufacturer;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = chip->batt_info.model;
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		val->strval = chip->batt_info.serial_num;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		error = fg_get_capacity(chip, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		error = fg_get_param(chip, FG_DATA_CURRENT, &val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		error = fg_get_param(chip, FG_DATA_VOLTAGE, &val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		error = fg_get_param(chip, FG_DATA_OCV, &val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = chip->batt_info.batt_max_voltage_uv;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		error = fg_get_temperature(chip, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		error = fg_get_param(chip, FG_DATA_CYCLE_COUNT, &val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		val->intval = 3000000; /* single-cell li-ion low end */
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		error = fg_get_param(chip, FG_DATA_CHARGE_COUNTER, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = chip->batt_info.nom_cap_uah;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = chip->cl.learned_cc_uah;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = chip->cl.init_cc_uah;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		val->intval = chip->dt.chg_term_current_ma * 1000;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = chip->status;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = fg_get_health_status(chip);
		break;
	default:
		pr_err("invalid property: %d\n", psp);
		return -EINVAL;
	}
	return error;
}

static int fg_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct fg_chip *chip = power_supply_get_drvdata(psy);
	//int error = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		chip->status = val->intval;
		schedule_work(&chip->status_change_work);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		chip->health = val->intval;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int fg_writable_property(struct power_supply *psy,
		enum power_supply_property psp)
{
	return 0;
}

static const struct power_supply_desc bms_psy_desc = {
	.name = "bms",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = fg_properties,
	.num_properties = ARRAY_SIZE(fg_properties),
	.get_property = fg_get_property,
	.set_property = fg_set_property,
	.property_is_writeable = fg_writable_property,
};

static int fg_probe(struct platform_device *pdev)
{
	struct power_supply_config bms_cfg = {};
	struct fg_pmic_data const *match_data;
	struct fg_chip *chip;
	int rc;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	mutex_init(&chip->sram_rw_lock);
	mutex_init(&chip->cl.lock);
	mutex_init(&chip->rslow_comp.lock);
	mutex_init(&chip->cyc_ctr.lock);
	init_completion(&chip->first_soc_done);
	init_completion(&chip->sram_access_granted);
	init_completion(&chip->sram_access_revoked);
	INIT_DELAYED_WORK(&chip->profile_load_work,
			fg_battery_profile_load_work);

	chip->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chip->regmap) {
		dev_err(&pdev->dev, "failed to locate the regmap\n");
		return -ENODEV;
	}

	rc = of_property_read_u32(pdev->dev.of_node, "reg", &chip->soc_base);
	if (rc) {
		dev_err(chip->dev, "missing or invalid 'reg' property\n");
		return rc;
	}

	chip->batt_base = chip->soc_base + 0x100;
	chip->mem_base = chip->soc_base + 0x400;

	match_data = device_get_match_data(chip->dev);
	chip->pmic_version = match_data->pmic_version;
	chip->param = match_data->params;
	chip->irqs = match_data->irqs;
	chip->do_restart = match_data->do_restart;
	chip->rconn_config = match_data->rconn_config;
	INIT_WORK(&chip->status_change_work, match_data->status_change_work);

	chip->status = POWER_SUPPLY_STATUS_DISCHARGING;

	rc = fg_init_memif(chip);
	if (rc) {
		dev_err(chip->dev, "failed to init memif: %d\n", rc);
		return rc;
	}

	rc = fg_of_init(chip);
	if (rc) {
		dev_err(chip->dev, "failed to get config from DTS: %d\n", rc);
		return rc;
	}

	rc = fg_hw_init(chip);
	if (rc) {
		dev_err(chip->dev, "failed to init hw: %d\n", rc);
		return rc;
	}
	
	rc = match_data->init_irqs(chip);
	if (rc) {
		dev_err(chip->dev, "failed to init irqs: %d\n", rc);
		return rc;
	}

	bms_cfg.drv_data = chip;
	bms_cfg.of_node = pdev->dev.of_node;

	chip->bms_psy = devm_power_supply_register(chip->dev,
			&bms_psy_desc, &bms_cfg);
	if (IS_ERR(chip->bms_psy)) {
		dev_err(&pdev->dev, "failed to register battery\n");
		return PTR_ERR(chip->bms_psy);
	}
	chip->power_supply_registered = true;

	platform_set_drvdata(pdev, chip);

	schedule_delayed_work(&chip->profile_load_work, 0);

	return 0;
}

static int fg_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct fg_pmic_data pmi8950_fg = {
	.pmic_version		= PMI8950,
	.params			= fg_params_pmi8950,
	.irqs			= fg_irqs_pmi8950,
	.init_irqs		= fg_init_irqs_pmi8950,
	.status_change_work	= status_change_work_pmi8950,
	.do_restart		= fg_do_restart_pmi8950,
	.rconn_config		= fg_rconn_config_pmi8950,
}, pmi8998v1_fg = {
	.pmic_version		= PMI8998_V1,
	.params			= fg_params_pmi8998_v1,
	.irqs			= fg_irqs_pmi8998,
	.init_irqs		= fg_init_irqs_pmi8998,
	.status_change_work	= status_change_work_pmi8998,
	.do_restart		= fg_do_restart_pmi8998,
	.rconn_config		= fg_rconn_config_pmi8998,
}, pmi8998v2_fg = {
	.pmic_version		= PMI8998_V2,
	.params			= fg_params_pmi8998_v2,
	.irqs			= fg_irqs_pmi8998,
	.init_irqs		= fg_init_irqs_pmi8998,
	.status_change_work	= status_change_work_pmi8998,
	.do_restart		= fg_do_restart_pmi8998,
	.rconn_config		= fg_rconn_config_pmi8998,
};

static const struct of_device_id fg_match_id_table[] = {
	{ .compatible = "qcom,pmi8950-fg", .data = &pmi8950_fg },
	{ .compatible = "qcom,pmi8998-v1-fg", .data = &pmi8998v1_fg },
	{ .compatible = "qcom,pmi8998-v2-fg", .data = &pmi8998v2_fg },
	{ }
};

MODULE_DEVICE_TABLE(of, fg_match_id_table);

static struct platform_driver qcom_fg_driver = {
	.probe = fg_probe,
	.remove = fg_remove,
	.driver = {
		.name = "qcom-fg",
		.of_match_table = fg_match_id_table,
	},
};

module_platform_driver(qcom_fg_driver);

MODULE_DESCRIPTION("Qualcomm Fuel Guage Driver");
MODULE_LICENSE("GPL v2");
