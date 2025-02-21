// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include <dt-bindings/clock/qcom,gcc-qsd8250.h>
//#include <dt-bindings/reset/qcom,gcc-qsd8250.h>

#include "common.h"
#include "clk-regmap.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "reset.h"

#if 0
case 0: /* TCX0 19200000 Hz */
   ret=-1;
  break;
  case 1: /* PLL1 */
   ret=1;
  break;
  case 4: /* PLL0 */
#endif

enum {
	P_TCXO,
	P_PLL1,
	P_PLL0 = 4,
};
/*
static struct fixed_clk tcxo_clk = {
	.c = {
		.dbg_name = "tcxo_clk",
		.rate = 19200000,
		.ops = &clk_ops_tcxo,
		CLK_INIT(tcxo_clk.c),
	},
};
*/


/*
// not a pll
static struct clk_pll tcxo = {
	.l_reg = 0x3144,
	.m_reg = 0x3148,
	.n_reg = 0x314c,
	.config_reg = 0x3154,
	.mode_reg = 0x3140,
	.status_reg = 0x3158,
	.status_bit = 16,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "tcxo",
		.parent_data = &(const struct clk_parent_data){
			.fw_name = "tcxo", .name = "tcxo_board",
		},
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};*/ 

// TODO: Get correct values from somwhere?
static struct clk_pll pll0 = {
	.l_reg = 0x3144,
	.m_reg = 0x3148,
	.n_reg = 0x314c,
	.config_reg = 0x3154,
	.mode_reg = 0x3140,
	.status_reg = 0x3158,
	.status_bit = 16,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pll0",
		.parent_data = &(const struct clk_parent_data){
			.fw_name = "pxo", .name = "pxo_board",
		},
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_regmap pll0_vote = {
	.enable_reg = //0x34c0,
	.enable_mask = BIT(8),
	.hw.init = &(struct clk_init_data){
		.name = "pll0_vote",
		.parent_hws = (const struct clk_hw*[]){
			&pll0.clkr.hw
		},
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

/**
 * struct parent_map - map table for source select configuration values
 * @src: source
 * @cfg: configuration value
 *
struct parent_map {
	u8 src;
	u8 cfg;
};
*/

static const struct parent_map gcc_tcxo_pll0_map[] = {
	{ P_TCXO, 0 },
	{ P_PLL0, 4 }
};

/**
 * struct clk_parent_data - clk parent information
 * @hw: parent clk_hw pointer (used for clk providers with internal clks)
 * @fw_name: parent name local to provider registering clk
 * @name: globally unique parent name (used as a fallback)
 * @index: parent index local to provider registering clk (if @fw_name absent)
 *
struct clk_parent_data {
	const struct clk_hw	*hw;
	const char		*fw_name;
	const char		*name;
	int			index;
};*/

static const struct clk_parent_data gcc_tcxo_pll0[] = {
	{ .fw_name = "tcxo", .name = "tcxo_board" },
	{ .hw = &pll0_vote.hw },
};

static const struct parent_map gcc_tcxo_pll1_pll0_map[] = {
	{ P_TCXO, 0 },
	{ P_PLL1, 1 },
	{ P_PLL0, 4 }
};

static const struct clk_parent_data gcc_tcxo_pll1_pll0[] = {
	{ .fw_name = "tcxo", .name = "tcxo_board" },
	{ .hw = &pll1_vote.hw },
	{ .hw = &pll0.clkr.hw },
};

/*
struct msm_clock_params
{
	unsigned clk_id;
	uint32_t glbl;	  // Whitch config reg GLBL_CLK_ENA or GLBL_CLK_ENA_2
	unsigned idx;
	unsigned offset;  // Offset points to .ns register
	unsigned ns_only; // value to fill in ns register, rather than using mdns_clock_params look-up table
	char	*name;
};
*/

/*
struct freq_tbl {
	unsigned long freq;
	u8 src; //glbl?
	u8 pre_div;
	u16 m; //md?
	u16 n; //ns?
};
*/

#if 0
struct mdns_clock_params
{
	unsigned long freq;
	uint32_t calc_freq;
	uint32_t md;
	uint32_t ns;
	uint32_t pll_freq;
	uint32_t clk_id;
};

struct mdns_clock_params msm_clock_freq_parameters[] = {

	MSM_CLOCK_REG(  144000,   3, 0x64, 0x32, 3, 3, 0, 1, 19200000), /* SD, 144kHz */
	MSM_CLOCK_REG(  400000,   1, 0x30, 0x15, 0, 3, 0, 1, 19200000), /* SD, 400kHz */
#if 0 /* wince uses this clock setting for UART2DM */
	MSM_CLOCK_REG( 1843200,     3, 0x64, 0x32, 3, 2, 4, 1, 245760000), /*  115200*16=1843200 */
//	MSM_CLOCK_REG(            , 2, 0xc8, 0x64, 3, 2, 1, 1, 768888888), /* 1.92MHz for 120000 bps */
#else
	MSM_CLOCK_REG( 7372800,   3, 0x64, 0x32, 0, 2, 4, 1, 245760000), /*  460800*16, will be divided by 4 for 115200 */
#endif
	MSM_CLOCK_REG(12000000,   1, 0x20, 0x10, 1, 3, 1, 1, 768000000), /* SD, 12MHz */
	MSM_CLOCK_REG(14745600,   3, 0x32, 0x19, 0, 2, 4, 1, 245760000), /* BT, 921600 (*16)*/
	MSM_CLOCK_REG(19200000,   1, 0x0a, 0x05, 3, 3, 1, 1, 768000000), /* SD, 19.2MHz */
	MSM_CLOCK_REG(24000000,   1, 0x10, 0x08, 1, 3, 1, 1, 768000000), /* SD, 24MHz */
	MSM_CLOCK_REG(24576000,   1, 0x0a, 0x05, 0, 2, 4, 1, 245760000), /* SD, 24,576000MHz */
	MSM_CLOCK_REG(25000000,  14, 0xd7, 0x6b, 1, 3, 1, 1, 768000000), /* SD, 25MHz */
	MSM_CLOCK_REG(32000000,   1, 0x0c, 0x06, 1, 3, 1, 1, 768000000), /* SD, 32MHz */
	MSM_CLOCK_REG(48000000,   1, 0x08, 0x04, 1, 3, 1, 1, 768000000), /* SD, 48MHz */
	MSM_CLOCK_REG(50000000,  25, 0xc0, 0x60, 1, 3, 1, 1, 768000000), /* SD, 50MHz */
	MSM_CLOCK_REG(58982400,   6, 0x19, 0x0c, 0, 2, 4, 1, 245760000), /* BT, 3686400 (*16) */
	MSM_CLOCK_REG(64000000,0x19, 0x60, 0x30, 0, 2, 4, 1, 245760000), /* BT, 4000000 (*16) */
};

// This formula is used to generate md and ns reg values
#define MSM_CLOCK_REG(frequency,M,N,D,PRE,a5,SRC,MNE,pll_frequency) { \
	.freq = (frequency), \
	.md = ((0xffff & (M)) << 16) | (0xffff & ~((D) << 1)), \
	.ns = ((0xffff & ~((N) - (M))) << 16) \
	    | ((0xff & (0xa | (MNE))) << 8) \
	    | ((0x7 & (a5)) << 5) \
	    | ((0x3 & (PRE)) << 3) \
	    | (0x7 & (SRC)), \
	.pll_freq = (pll_frequency), \
	.calc_freq = 1000*((pll_frequency/1000)*M/((PRE+1)*N)), \
}
#endif

// SDC2
// TODO: Previv, md, ns
static const struct freq_tbl clk_tbl_sdc[] = {
	freq,  src=glbl, pre_div; md; ns
	{    144000, P_TCXO,  0, 0, 0 },
	{    400000, P_TCXO,  0, 0, 0 },
	{  12000000, P_PLL1,  0, 0, 0 },
	{  19200000, P_PLL1,  0, 0, 0 },
	{  24000000, P_PLL1,  0, 0, 0 },
	{  24576000, P_PLL0,  0, 0, 0 },
	{  25000000, P_PLL1,  0, 0, 0 },
	{  32000000, P_PLL1,  0, 0, 0 },
	{  48000000, P_PLL1,  0, 0, 0 },
	{  50000000, P_PLL1,  0, 0, 0 },
	{ }
};

//https://github.com/chpec/uboot-ac100/blob/5a7311d2f811b2044a39b2d9293fab453912b2ee/arch/arm/include/asm/arch-QSD8x50/QSD8x50_reg.h#L283
#define SDC2_MD_REG (0xA86000A8)
#define SDC2_NS_REG (0xA86000AC)

//https://github.com/samsung-msm8974/kernel_samsung_msm8974/blob/026c92d2418e76ce150fd1f0f91a4c0cace22aa3/arch/arm/mach-msm/clock-7x30.c#L1419

// TODO: Correct all regs / vars
// Also should those two be sdc2_pclk + sdc2_clk instead?
static struct clk_rcg sdc2_src = {
	.ns_reg = 0x00ac,//shoud be ok
	.md_reg = 0x00a8,//ditto
	.mn = {
		.mnctr_en_bit = 8,//?
		.mnctr_reset_bit = 7,//?
		.mnctr_mode_shift = 5,//?
		.n_val_shift = 16,//?
		.m_val_shift = 16,//?
		.width = 8,//?
	},
	.p = {
		.pre_div_shift = 3,//?
		.pre_div_width = 2,//?
	},
	.s = {
		.src_sel_shift = 0,//?
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_sdc,
	.clkr = {
		.enable_reg = 0x00ac,
		.enable_mask = BIT(11),//same for 7x30 it seems but check
		.hw.init = &(struct clk_init_data){
			.name = "sdc2_src",
			.parent_data = gcc_tcxo_pll1_pll0,
			.num_parents = ARRAY_SIZE(gcc_tcxo_pll1_pll0),
			.ops = &clk_rcg_ops,
		},
	}
};

// TODO: Correct all regs / vars
static struct clk_branch sdc2_clk = {
	.halt_reg = 0x2fc8,
	.halt_bit = 5,
	.clkr = {
		.enable_reg = 0x284c,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "sdc2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&sdc2_src.clkr.hw
			},
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

// CLKS
static struct clk_regmap *gcc_qsd8250_clks[] = {
	//[PLL0] = &pll0.clkr,
	//[PLL0_VOTE] = &pll0_vote,
	[SDC2_PCLK] = &sdc2_src.clkr,//sdc2_src?
	[SDC2_CLK] = &sdc2_clk.clkr,
	//[USB_HS1_XCVR_SRC] = &usb_hs1_xcvr_src.clkr,
	//[USB_HS1_XCVR_CLK] = &usb_hs1_xcvr_clk.clkr,
	//[USB_FS1_XCVR_FS_SRC] = &usb_fs1_xcvr_fs_src.clkr,
	//[USB_FS1_XCVR_FS_CLK] = &usb_fs1_xcvr_fs_clk.clkr,
	//[USB_FS1_SYSTEM_CLK] = &usb_fs1_system_clk.clkr,
	//[USB_FS2_XCVR_FS_SRC] = &usb_fs2_xcvr_fs_src.clkr,
	//[USB_FS2_XCVR_FS_CLK] = &usb_fs2_xcvr_fs_clk.clkr,
	//[USB_FS2_SYSTEM_CLK] = &usb_fs2_system_clk.clkr,
	//[USB_FS1_H_CLK] = &usb_fs1_h_clk.clkr,
	//[USB_FS2_H_CLK] = &usb_fs2_h_clk.clkr,
	//[USB_HS1_H_CLK] = &usb_hs1_h_clk.clkr,
	//[SDC2_H_CLK] = &sdc2_h_clk.clkr,
	//[EBI2_CLK] = &ebi2_clk.clkr,
	//[ADM0_CLK] = &adm0_clk.clkr,
	//[ADM0_PBUS_CLK] = &adm0_pbus_clk.clkr,
	//[ADM1_CLK] = &adm1_clk.clkr,
	//[ADM1_PBUS_CLK] = &adm1_pbus_clk.clkr,
};

// What are resets here?
// TODO: Correct sdc2 reset
static const struct qcom_reset_map gcc_qsd8250_resets[] = {
	//               reg   , bit;
	[SDC2_RESET] = { 0x2850 },
	//[USB_HS1_RESET] = { 0x2910 },
	//[USB_HS2_XCVR_RESET] = { 0x2934, 1 },
	//[USB_HS2_RESET] = { 0x2934 },
	//[USB_FS1_XCVR_RESET] = { 0x2974, 1 },
	//[USB_FS1_RESET] = { 0x2974 },
	//[USB_FS2_XCVR_RESET] = { 0x2994, 1 },
	//[USB_FS2_RESET] = { 0x2994 },
	//[MARRM_PWRON_RESET] = { 0x2bd4, 1 },
	//[MARM_RESET] = { 0x2bd4 },
	//[MAHB1_RESET] = { 0x2be4, 7 },
	//[USB_PHY0_RESET] = { 0x2e20 },
	//[USB_PHY1_RESET] = { 0x2e40 },
};

// Correct / check values
static const struct regmap_config gcc_qsd8250_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x363c,
	.fast_io	= true,
};

static const struct qcom_cc_desc gcc_qsd8250_desc = {
	.config = &gcc_qsd8250_regmap_config,
	.clks = gcc_qsd8250_clks,
	.num_clks = ARRAY_SIZE(gcc_qsd8250_clks),
	.resets = gcc_qsd8250_resets,
	.num_resets = ARRAY_SIZE(gcc_qsd8250_resets),
};

static const struct of_device_id gcc_qsd8250_match_table[] = {
	{ .compatible = "qcom,gcc-qsd8250" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_qsd8250_match_table);

static int gcc_qsd8250_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &gcc_qsd8250_desc);
}

static struct platform_driver gcc_qsd8250_driver = {
	.probe		= gcc_qsd8250_probe,
	.driver		= {
		.name	= "gcc-qsd8250",
		.of_match_table = gcc_qsd8250_match_table,
	},
};

// All init disabled for now for safety measures
static int __init gcc_qsd8250_init(void)
{
	pr_err("gcc-qsd8250: probe called, hanging here\n");
	for(;;) {};
	return 0;
	//return platform_driver_register(&gcc_qsd8250_driver);
}
core_initcall(gcc_qsd8250_init);

static void __exit gcc_qsd8250_exit(void)
{
	platform_driver_unregister(&gcc_qsd8250_driver);
}
module_exit(gcc_qsd8250_exit);

MODULE_DESCRIPTION("GCC QSD 8250 Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:gcc-qsd8250");
