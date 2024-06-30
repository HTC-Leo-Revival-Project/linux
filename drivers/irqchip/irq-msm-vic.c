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
#include <linux/irqdomain.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/cacheflush.h>
#include <asm/exception.h>
#include <asm/irq.h>

/* These definitions correspond to the "new mapping" of the
 * register set that interleaves "high" and "low". The offsets
 * below are for the "low" register, add 4 to get to the high one
 */
#define VIC_INT_SELECT      0x0000  /* 1: FIQ, 0: IRQ */
#define VIC_INT_EN          0x0010
#define VIC_INT_ENCLEAR     0x0020
#define VIC_INT_ENSET       0x0030
#define VIC_INT_TYPE        0x0040  /* 1: EDGE, 0: LEVEL  */
#define VIC_INT_POLARITY    0x0050  /* 1: NEG, 0: POS */
#define VIC_INT_MASTEREN    0x0068  /* 1: IRQ, 2: FIQ     */
#define VIC_CONFIG          0x006C  /* 1: USE SC VIC */
#define VIC_INT_CLEAR       0x00B0
#define VIC_IRQ_VEC_RD      0x00D0  /* pending int # */
#define VIC_IRQ_VEC_PEND_RD 0x00D4  /* pending vector addr */
#define VIC_IRQ_VEC_WR      0x00D8

#define VIC_INT_TO_REG_ADDR(base, irq) (base + ((irq & 32) ? 4 : 0))

void __iomem *vic_base;
static struct irq_domain *domain;

static void msm_irq_ack(struct irq_data *d)
{
	void __iomem *reg = VIC_INT_TO_REG_ADDR(vic_base + VIC_INT_CLEAR, d->irq);
	writel(1 << (d->irq & 31), reg);
}

static void msm_irq_mask(struct irq_data *d)
{
	void __iomem *reg = VIC_INT_TO_REG_ADDR(vic_base + VIC_INT_ENCLEAR, d->irq);
	uint32_t mask = 1UL << (d->irq & 31);

	writel(mask, reg);
}

static void msm_irq_unmask(struct irq_data *d)
{
	void __iomem *reg = VIC_INT_TO_REG_ADDR(vic_base + VIC_INT_ENSET, d->irq);
	uint32_t mask = 1UL << (d->irq & 31);

	writel(mask, reg);
}

static void __exception_irq_entry vic_handle_irq(struct pt_regs *regs)
{
	u32 irqnr;
	do {
		/* VIC_IRQ_VEC_RD has irq# or old irq# if the irq has been handled
		 * VIC_IRQ_VEC_PEND_RD has irq# or -1 if none pending *but* if you 
		 * just read VIC_IRQ_VEC_PEND_RD you never get the first irq for some reason
		 */
		irqnr = readl_relaxed(vic_base + VIC_IRQ_VEC_RD);
		irqnr = readl_relaxed(vic_base + VIC_IRQ_VEC_PEND_RD);
		if (irqnr == -1)
			break;
		handle_IRQ(irqnr, regs);
	} while (1);
}

static struct irq_chip msm_irq_chip = {
	.name          = "msm",
	.irq_disable   = msm_irq_mask,
	.irq_ack       = msm_irq_ack,
	.irq_mask      = msm_irq_mask,
	.irq_unmask    = msm_irq_unmask,
};

static int msm_vic_map(struct irq_domain *d, unsigned int irq,
		       irq_hw_number_t hw)
{
	irq_set_chip_and_handler(irq, &msm_irq_chip, handle_level_irq);
	irq_set_chip_data(irq, vic_base);

	return 0;
}

static const struct irq_domain_ops msm_vic_irqchip_intc_ops = {
	.xlate = irq_domain_xlate_onetwocell,
	.map = msm_vic_map,
};

static int __init msm_init_irq(struct device_node *node, struct device_node *parent)
{
	vic_base = of_iomap(node, 0);

	if (!vic_base){
		panic("%pOF: unable to map local interrupt registers\n", node);
	}

	/* select level interrupts */
	writel(0, vic_base + VIC_INT_TYPE);
	writel(0, vic_base + VIC_INT_TYPE + 4);

	/* select highlevel interrupts */
	writel(0, vic_base + VIC_INT_POLARITY);
	writel(0, vic_base + VIC_INT_POLARITY + 4);

	/* select IRQ for all INTs */
	writel(0, vic_base + VIC_INT_SELECT);
	writel(0, vic_base + VIC_INT_SELECT + 4);

	/* disable all INTs */
	writel(0, vic_base + VIC_INT_EN);
	writel(0, vic_base + VIC_INT_EN + 4);

	/* don't use vic */
	writel(0, vic_base + VIC_CONFIG);

	/* enable interrupt controller */
	writel(3, vic_base + VIC_INT_MASTEREN);

	domain = irq_domain_add_legacy(node, 64,
					       0, 0,
					       &msm_vic_irqchip_intc_ops, NULL);
	if (!domain)
		panic("Unable to add VIC IRQ domain\n");
	irq_set_default_host(domain);

	/* Ready to receive interrupts */
	set_handle_irq(vic_handle_irq);

	return 0;
}

IRQCHIP_DECLARE(arm_msm_vic, "arm,msm-vic", msm_init_irq);