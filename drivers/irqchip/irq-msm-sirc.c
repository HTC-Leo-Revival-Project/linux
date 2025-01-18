/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/cacheflush.h>
#include <asm/exception.h>
#include <asm/irq.h>

#define NR_SIRC_IRQS                  23
#define SIRC_MASK                     0x007FFFFF

#define FIRST_SIRC_IRQ                64

#define SIRC_INT_SELECT          0x00
#define SIRC_INT_ENABLE          0x04
#define SIRC_INT_ENABLE_CLEAR    0x08
#define SIRC_INT_ENABLE_SET      0x0C
#define SIRC_INT_TYPE            0x10
#define SIRC_INT_POLARITY        0x14
#define SIRC_SECURITY            0x18
#define SIRC_IRQ_STATUS          0x1C
#define SIRC_IRQ1_STATUS         0x20
#define SIRC_RAW_STATUS          0x24
#define SIRC_INT_CLEAR           0x28
#define SIRC_SOFT_INT            0x2C

#define NUM_SIRC_REGS 2

struct msm_sirc {
	void __iomem		*base;
	int					parent_irq;
	struct irq_domain	*domain;
	u32					int_enable;
	u32					wake_enable;
};

/* Mask off the given interrupt. Keep the int_enable mask in sync with
   the enable reg, so it can be restored after power collapse. */
static void sirc_irq_mask(struct irq_data *d)
{
	struct msm_sirc *sirc = irq_data_get_irq_chip_data(d);
	unsigned int mask = BIT(d->hwirq);

	writel(mask, sirc->base + SIRC_INT_ENABLE_CLEAR);
	sirc->int_enable &= ~mask;
	return;
}

/* Unmask the given interrupt. Keep the int_enable mask in sync with
   the enable reg, so it can be restored after power collapse. */
static void sirc_irq_unmask(struct irq_data *d)
{
	struct msm_sirc *sirc = irq_data_get_irq_chip_data(d);
	unsigned int mask = BIT(d->hwirq);

	writel(mask, sirc->base + SIRC_INT_ENABLE_SET);
	sirc->int_enable |= mask;
	return;
}

static void sirc_irq_ack(struct irq_data *d)
{
	struct msm_sirc *sirc = irq_data_get_irq_chip_data(d);
	unsigned int mask = BIT(d->hwirq);

	writel(mask, sirc->base + SIRC_INT_CLEAR);
	return;
}

static int sirc_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct msm_sirc *sirc = irq_data_get_irq_chip_data(d);
	unsigned int mask = BIT(d->hwirq);

	irq_set_irq_wake(d->hwirq, on);

	/* Used to set the interrupt enable mask during power collapse. */
	if (on)
		sirc->wake_enable |= mask;
	else
		sirc->wake_enable &= ~mask;

	return 0;
}

static int sirc_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct msm_sirc *sirc = irq_data_get_irq_chip_data(d);
	unsigned int mask = BIT(d->hwirq);
	unsigned int val;

	val = readl(sirc->base + SIRC_INT_POLARITY);

	if (flow_type & (IRQF_TRIGGER_LOW | IRQF_TRIGGER_FALLING))
		val |= mask;
	else
		val &= ~mask;

	writel(val, sirc->base + SIRC_INT_POLARITY);

	val = readl(sirc->base + SIRC_INT_TYPE);
	if (flow_type & (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING)) {
		val |= mask;
		irq_set_handler_locked(d, handle_edge_irq);
	} else {
		val &= ~mask;
		irq_set_handler_locked(d, handle_level_irq);
	}

	writel(val, sirc->base + SIRC_INT_TYPE);

	return 0;
}

/* Finds the pending interrupt on the passed cascade irq and redrives it */
static void sirc_irq_handler(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct msm_sirc *sirc = irq_desc_get_handler_data(desc);
	unsigned int sirq;
	unsigned int status;
	
	/*
	while ((reg < ARRAY_SIZE(sirc_reg_table)) &&
		(sirc_reg_table[reg].cascade_irq != irq))
		reg++;

	if (reg == ARRAY_SIZE(sirc_reg_table)) {
		printk(KERN_ERR "%s: incorrect irq %d called\n",
			__func__, irq);
		return;
	}*/

	chained_irq_enter(chip, desc);

	status = readl(sirc->base + SIRC_IRQ_STATUS);
	status &= SIRC_MASK;
	if (status == 0)
		return;

	/*
	for (sirq = 0;
	     (sirq < NR_SIRC_IRQS) && ((status & (1U << sirq)) == 0);
	     sirq++)
		;
		generic_handle_irq(sirq+FIRST_SIRC_IRQ);
	*/
	for (sirq = 0; (sirq < NR_SIRC_IRQS); sirq++) {
		if((status & (1U << sirq)) != 0) {
			generic_handle_domain_irq(sirc->domain, sirq);
		}
	}

	desc->irq_data.chip->irq_ack(&desc->irq_data);

	chained_irq_exit(chip, desc);
}

static struct irq_chip sirc_irq_chip = {
	.name          = "sirc",
	.irq_ack       = sirc_irq_ack,
	.irq_mask      = sirc_irq_mask,
	.irq_unmask    = sirc_irq_unmask,
	.irq_set_wake  = sirc_irq_set_wake,
	.irq_set_type  = sirc_irq_set_type,
};

static int msm_sirc_map(struct irq_domain *d, unsigned int irq,
		       irq_hw_number_t hw)
{
	irq_set_chip_and_handler(irq, &sirc_irq_chip, handle_edge_irq);
	irq_set_chip_data(irq, d->host_data);
	irq_set_noprobe(irq);

	return 0;
}

static const struct irq_domain_ops msm_sirc_irqchip_intc_ops = {
	.xlate = irq_domain_xlate_onetwocell,
	.map = msm_sirc_map,
};

static int __init msm_init_sirc(struct device_node *node, struct device_node *parent)
{
	int irq_base;

	struct msm_sirc *sirc;
	sirc = kzalloc(sizeof(*sirc), GFP_KERNEL);
	if (!sirc)
		return -ENOMEM;

	sirc->base = of_iomap(node, 0);
    if (!sirc->base){
		panic("%pOF: unable to map sirc interrupt registers\n", node);
	}

    irq_base = irq_alloc_descs(-1, FIRST_SIRC_IRQ, NR_SIRC_IRQS, 0);
	if (irq_base < 0) {
		pr_warn("Couldn't allocate IRQ numbers\n");
        irq_base = 0;
	}

    sirc->domain = irq_domain_add_legacy(node, NR_SIRC_IRQS,
					       irq_base, 0,
					       &msm_sirc_irqchip_intc_ops, sirc);
	if (!sirc->domain)
		panic("Unable to add SIRC IRQ domain\n");

	/* Map the parent interrupt for the chained handler */
	sirc->parent_irq = irq_of_parse_and_map(node, 0);
	if (sirc->parent_irq <= 0) {
		pr_err("%pOF: unable to parse sirc irq\n", node);
		return -EINVAL;
	}

    if (request_irq(sirc->parent_irq, no_action, IRQF_NO_THREAD, "cascade", NULL))
		pr_err("Failed to register cascade interrupt\n");

    irq_set_chained_handler_and_data(sirc->parent_irq, 
					sirc_irq_handler,
					sirc);

	return 0;
}

IRQCHIP_DECLARE(arm_msm_sirc, "arm,msm-sirc", msm_init_sirc);