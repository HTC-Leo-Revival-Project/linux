/*
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2009, Code Aurora Forum. All rights reserved.
 * Copyright (c) 2024, Htc Leo Revival Project
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/cacheflush.h>
#include <asm/exception.h>
#include <asm/irq.h>

#include "irqs-qsd8k.h"

enum {
	IRQ_DEBUG_SLEEP_INT_TRIGGER = 1U << 0,
	IRQ_DEBUG_SLEEP_INT = 1U << 1,
	IRQ_DEBUG_SLEEP_ABORT = 1U << 2,
	IRQ_DEBUG_SLEEP = 1U << 3,
	IRQ_DEBUG_SLEEP_REQUEST = 1U << 4,
};
static int msm_irq_debug_mask;
module_param_named(debug_mask, msm_irq_debug_mask, int,
		   S_IRUGO | S_IWUSR | S_IWGRP);

#define VIC_INT_TO_REG_ADDR(base, irq) (base + ((irq & 32) ? 4 : 0))
#define VIC_INT_TO_REG_INDEX(irq) ((irq >> 5) & 1)

#define VIC_INT_SELECT0     0x0000  /* 1: FIQ, 0: IRQ */
#define VIC_INT_SELECT1     0x0004  /* 1: FIQ, 0: IRQ */
#define VIC_INT_SELECT2     0x0008  /* 1: FIQ, 0: IRQ */
#define VIC_INT_SELECT3     0x000C  /* 1: FIQ, 0: IRQ */
#define VIC_INT_EN0         0x0010
#define VIC_INT_EN1         0x0014
#define VIC_INT_EN2         0x0018
#define VIC_INT_EN3         0x001C
#define VIC_INT_ENCLEAR0    0x0020
#define VIC_INT_ENCLEAR1    0x0024
#define VIC_INT_ENCLEAR2    0x0028
#define VIC_INT_ENCLEAR3    0x002C
#define VIC_INT_ENSET0      0x0030
#define VIC_INT_ENSET1      0x0034
#define VIC_INT_ENSET2      0x0038
#define VIC_INT_ENSET3      0x003C
#define VIC_INT_TYPE0       0x0040  /* 1: EDGE, 0: LEVEL  */
#define VIC_INT_TYPE1       0x0044  /* 1: EDGE, 0: LEVEL  */
#define VIC_INT_TYPE2       0x0048  /* 1: EDGE, 0: LEVEL  */
#define VIC_INT_TYPE3       0x004C  /* 1: EDGE, 0: LEVEL  */
#define VIC_INT_POLARITY0   0x0050  /* 1: NEG, 0: POS */
#define VIC_INT_POLARITY1   0x0054  /* 1: NEG, 0: POS */
#define VIC_INT_POLARITY2   0x0058  /* 1: NEG, 0: POS */
#define VIC_INT_POLARITY3   0x005C  /* 1: NEG, 0: POS */
#define VIC_NO_PEND_VAL     0x0060


#define VIC_NO_PEND_VAL_FIQ 0x0064
#define VIC_INT_MASTEREN    0x0068  /* 1: IRQ, 2: FIQ     */
#define VIC_CONFIG          0x006C  /* 1: USE SC VIC */


#define IRQF_VALID	(1 << 0)
#define IRQF_PROBE	(1 << 1)
#define IRQF_NOAUTOEN	(1 << 2)

#define VIC_IRQ_STATUS0     0x0080
#define VIC_IRQ_STATUS1     0x0084
#define VIC_IRQ_STATUS2     0x0088
#define VIC_IRQ_STATUS3     0x008C
#define VIC_FIQ_STATUS0     0x0090
#define VIC_FIQ_STATUS1     0x0094
#define VIC_FIQ_STATUS2     0x0098
#define VIC_FIQ_STATUS3     0x009C
#define VIC_RAW_STATUS0     0x00A0
#define VIC_RAW_STATUS1     0x00A4
#define VIC_RAW_STATUS2     0x00A8
#define VIC_RAW_STATUS3     0x00AC
#define VIC_INT_CLEAR0      0x00B0
#define VIC_INT_CLEAR1      0x00B4
#define VIC_INT_CLEAR2      0x00B8
#define VIC_INT_CLEAR3      0x00BC
#define VIC_SOFTINT0        0x00C0
#define VIC_SOFTINT1        0x00C4
#define VIC_SOFTINT2        0x00C8
#define VIC_SOFTINT3        0x00CC
#define VIC_IRQ_VEC_RD      0x00D0  /* pending int # */
#define VIC_IRQ_VEC_PEND_RD 0x00D4  /* pending vector addr */
#define VIC_IRQ_VEC_WR      0x00D8


#define VIC_FIQ_VEC_RD      0x00DC
#define VIC_FIQ_VEC_PEND_RD 0x00E0
#define VIC_FIQ_VEC_WR      0x00E4
#define VIC_IRQ_IN_SERVICE  0x00E8
#define VIC_IRQ_IN_STACK    0x00EC
#define VIC_FIQ_IN_SERVICE  0x00F0
#define VIC_FIQ_IN_STACK    0x00F4
#define VIC_TEST_BUS_SEL    0x00F8
#define VIC_IRQ_CTRL_CONFIG 0x00FC


#define VIC_VECTPRIORITY(n) 0x0200+((n) * 4)
#define VIC_VECTADDR(n)     0x0400+((n) * 4)


#define VIC_NUM_REGS	    2

#if VIC_NUM_REGS == 2
#define DPRINT_REGS(base_reg, format, ...)	      			\
	printk(KERN_INFO format " %x %x\n", ##__VA_ARGS__,		\
			readl(base_reg ## 0), readl(base_reg ## 1))
#define DPRINT_ARRAY(array, format, ...)				\
	printk(KERN_INFO format " %x %x\n", ##__VA_ARGS__,		\
			array[0], array[1])
#elif VIC_NUM_REGS == 4
#define DPRINT_REGS(base_reg, format, ...) \
	printk(KERN_INFO format " %x %x %x %x\n", ##__VA_ARGS__,	\
			readl(base_reg ## 0), readl(base_reg ## 1),	\
			readl(base_reg ## 2), readl(base_reg ## 3))
#define DPRINT_ARRAY(array, format, ...)				\
	printk(KERN_INFO format " %x %x %x %x\n", ##__VA_ARGS__,	\
			array[0], array[1],				\
			array[2], array[3])
#else
#error "VIC_NUM_REGS set to illegal value"
#endif

static uint32_t msm_irq_smsm_wake_enable[2];
static struct {
	uint32_t int_en[2];
	uint32_t int_type;
	uint32_t int_polarity;
	uint32_t int_select;
} msm_irq_shadow_reg[VIC_NUM_REGS];
static uint32_t msm_irq_idle_disable[VIC_NUM_REGS];

struct vic_device {
	void __iomem *base;
	struct irq_domain *domain;
};
static struct vic_device vic_data;

#define SMSM_FAKE_IRQ (0xff)
static uint8_t msm_irq_to_smsm[NR_IRQS] = {
	[INT_MDDI_EXT] = 1,
	[INT_MDDI_PRI] = 2,
	[INT_MDDI_CLIENT] = 3,
	[INT_USB_OTG] = 4,

	[INT_PWB_I2C] = 5,
	[INT_SDC1_0] = 6,
	[INT_SDC1_1] = 7,
	[INT_SDC2_0] = 8,

	[INT_SDC2_1] = 9,
	[INT_ADSP_A9_A11] = 10,

	[INT_UART1DM_IRQ] = 17,
	[INT_UART1DM_RX] = 18,
	[INT_KEYSENSE] = 19,
	[INT_AD_HSSD] = 20,

	[INT_NAND_WR_ER_DONE] = 21,
	[INT_NAND_OP_DONE] = 22,
	[INT_TCHSCRN1] = 23,
	[INT_TCHSCRN2] = 24,

	[INT_TCHSCRN_SSBI] = 25,
	[INT_USB_HS] = 26,
	[INT_UART2DM_RX] = 27,
	[INT_UART2DM_IRQ] = 28,

	[INT_SDC4_1] = 29,
	[INT_SDC4_0] = 30,
	[INT_SDC3_1] = 31,
	[INT_SDC3_0] = 32,

	/* fake wakeup interrupts */
	[INT_A9_M2A_0] = SMSM_FAKE_IRQ,
	[INT_A9_M2A_1] = SMSM_FAKE_IRQ,
	[INT_A9_M2A_5] = SMSM_FAKE_IRQ,
	[INT_GP_TIMER_EXP] = SMSM_FAKE_IRQ,
	[INT_DEBUG_TIMER_EXP] = SMSM_FAKE_IRQ,
	[INT_ADSP_A11] = SMSM_FAKE_IRQ,
	[INT_SIRC_0] = SMSM_FAKE_IRQ,
	[INT_SIRC_1] = SMSM_FAKE_IRQ,
};

void set_irq_flags(unsigned int irq, unsigned int iflags)
{
	unsigned long clr = 0, set = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;

	if (irq >= NR_MSM_IRQS) {
		pr_err("Trying to set irq flags for IRQ%d\n", irq);
		return;
	}

	if (iflags & IRQF_VALID)
		clr |= IRQ_NOREQUEST;
	if (iflags & IRQF_PROBE)
		clr |= IRQ_NOPROBE;
	if (!(iflags & IRQF_NOAUTOEN))
		clr |= IRQ_NOAUTOEN;
	/* Order is clear bits in "clr" then set bits in "set" */
	irq_modify_status(irq, clr, set & ~clr);
}
EXPORT_SYMBOL_GPL(set_irq_flags);


static inline void msm_irq_write_all_regs(void __iomem *base, unsigned int val)
{
	for (int i = 0; i < VIC_NUM_REGS; i++)
		writel(val, base + (i * 4));
}

static void msm_irq_ack(struct irq_data *d)
{
	void __iomem *reg = VIC_INT_TO_REG_ADDR(vic_data.base + VIC_INT_CLEAR0, d->irq);
	writel(1 << (d->irq & 31), reg);
}

static void msm_irq_mask(struct irq_data *d)
{
	void __iomem *reg = VIC_INT_TO_REG_ADDR(vic_data.base + VIC_INT_ENCLEAR0, d->irq);
	unsigned index = VIC_INT_TO_REG_INDEX(d->irq);
	uint32_t mask = 1UL << (d->irq & 31);
	int smsm_irq = msm_irq_to_smsm[d->irq];

	msm_irq_shadow_reg[index].int_en[0] &= ~mask;
	writel(mask, reg);
	if (smsm_irq == 0)
		msm_irq_idle_disable[index] &= ~mask;
	else {
		mask = 1UL << (smsm_irq - 1);
		msm_irq_smsm_wake_enable[0] &= ~mask;
	}
}

static void msm_irq_unmask(struct irq_data *d)
{
	void __iomem *reg = VIC_INT_TO_REG_ADDR(vic_data.base + VIC_INT_ENSET0, d->irq);
	unsigned index = VIC_INT_TO_REG_INDEX(d->irq);
	uint32_t mask = 1UL << (d->irq & 31);
	int smsm_irq = msm_irq_to_smsm[d->irq];

	msm_irq_shadow_reg[index].int_en[0] |= mask;
	writel(mask, reg);

	if (smsm_irq == 0)
		msm_irq_idle_disable[index] |= mask;
	else {
		mask = 1UL << (smsm_irq - 1);
		msm_irq_smsm_wake_enable[0] |= mask;
	}
}

static int msm_irq_set_wake(struct irq_data *d, unsigned int on)
{
	unsigned index = VIC_INT_TO_REG_INDEX(d->irq);
	uint32_t mask = 1UL << (d->irq & 31);
	int smsm_irq = msm_irq_to_smsm[d->irq];

	if (smsm_irq == 0) {
		printk(KERN_ERR "msm_irq_set_wake: bad wakeup irq %d\n", d->irq);
		return -EINVAL;
	}
	if (on)
		msm_irq_shadow_reg[index].int_en[1] |= mask;
	else
		msm_irq_shadow_reg[index].int_en[1] &= ~mask;

	if (smsm_irq == SMSM_FAKE_IRQ)
		return 0;

	mask = 1UL << (smsm_irq - 1);
	if (on)
		msm_irq_smsm_wake_enable[1] |= mask;
	else
		msm_irq_smsm_wake_enable[1] &= ~mask;
	return 0;
}

static int msm_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	void __iomem *treg = VIC_INT_TO_REG_ADDR(vic_data.base + VIC_INT_TYPE0, d->irq);
	void __iomem *preg = VIC_INT_TO_REG_ADDR(vic_data.base + VIC_INT_POLARITY0, d->irq);
	unsigned index = VIC_INT_TO_REG_INDEX(d->irq);
	int b = 1 << (d->irq & 31);
	uint32_t polarity;
	uint32_t type;

	polarity = msm_irq_shadow_reg[index].int_polarity;
	if (flow_type & (IRQF_TRIGGER_FALLING | IRQF_TRIGGER_LOW))
		polarity |= b;
	if (flow_type & (IRQF_TRIGGER_RISING | IRQF_TRIGGER_HIGH))
		polarity &= ~b;
	writel(polarity, preg);
	msm_irq_shadow_reg[index].int_polarity = polarity;

	type = msm_irq_shadow_reg[index].int_type;
	if (flow_type & (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING)) {
		type |= b;
		irq_set_handler_locked(d, handle_edge_irq);
	}
	if (flow_type & (IRQF_TRIGGER_HIGH | IRQF_TRIGGER_LOW)) {
		type &= ~b;
		irq_set_handler_locked(d, handle_level_irq);
	}
	writel(type, treg);
	msm_irq_shadow_reg[index].int_type = type;
	return 0;
}

static inline void msm_vic_handle_irq(void __iomem *base_addr, struct pt_regs
		*regs)
{
	u32 irqnr;
	do {
		/* VIC_IRQ_VEC_RD has irq# or old irq# if the irq has been handled
		 * VIC_IRQ_VEC_PEND_RD has irq# or -1 if none pending *but* if you 
		 * just read VIC_IRQ_VEC_PEND_RD you never get the first irq for some reason
		 */
		irqnr = readl_relaxed(base_addr + VIC_IRQ_VEC_RD);
		irqnr = readl_relaxed(base_addr + VIC_IRQ_VEC_PEND_RD);
		if (irqnr == -1)
			break;
		handle_IRQ(irqnr, regs);
	} while (1);
}

/* enable imprecise aborts */
#define local_cpsie_enable()  __asm__ __volatile__("cpsie a    @ enable")

static void __exception_irq_entry vic_handle_irq(struct pt_regs *regs)
{
	local_cpsie_enable();// local_abt_enable()?
	msm_vic_handle_irq(vic_data.base, regs);
}

static struct irq_chip msm_irq_chip = {
	.name          = "msm",
	.irq_disable   = msm_irq_mask,
	.irq_ack       = msm_irq_ack,
	.irq_mask      = msm_irq_mask,
	.irq_unmask    = msm_irq_unmask,
	.irq_set_wake  = msm_irq_set_wake,
	.irq_set_type  = msm_irq_set_type,
};

static int __init msm_init_irq(struct device_node *intc, struct device_node *parent)
{
	int ret;
	void __iomem *regs;
	uint32_t num_irqs;
	int i;
	regs = of_iomap(intc, 0);
	if (WARN_ON(!regs))
		return -EIO;
	vic_data.base = regs;
	ret = of_property_read_u32(intc, "num-irqs", &num_irqs);
	if (ret) {
		pr_err("%s: failed to read num-irqs ret=%d\n", __func__, ret);
		return ret;
	}

	ret = irq_alloc_descs(-1, 0, 64, 0);
	if (ret < 0) {
		pr_warn("Couldn't allocate IRQ numbers\n");
	}

	/* select level interrupts */
	msm_irq_write_all_regs(vic_data.base + VIC_INT_TYPE0, 0);

	/* select highlevel interrupts */
	msm_irq_write_all_regs(vic_data.base + VIC_INT_POLARITY0, 0);

	/* select IRQ for all INTs */
	msm_irq_write_all_regs(vic_data.base + VIC_INT_SELECT0, 0);

	/* disable all INTs */
	msm_irq_write_all_regs(vic_data.base + VIC_INT_EN0, 0);

	/* don't use vic */
	writel(0, vic_data.base + VIC_CONFIG);

	/* enable interrupt controller */
	writel(3, vic_data.base + VIC_INT_MASTEREN);

	for (int n = 0; n < NR_MSM_IRQS; n++) {
		irq_set_chip_and_handler(n, &msm_irq_chip, handle_level_irq);
		set_irq_flags(n, IRQF_VALID);
	}

	/* Ready to receive interrupts */
	set_handle_irq(vic_handle_irq);


	vic_data.domain = irq_domain_add_legacy(intc, num_irqs, 0, 0, &irq_domain_simple_ops, &vic_data);
	if (!vic_data.domain) {
		pr_err("%s: failed to register irq domain\n", __func__);
	}


	return 0;
}

IRQCHIP_DECLARE(arm_msm_vic, "arm,msm-vic", msm_init_irq);