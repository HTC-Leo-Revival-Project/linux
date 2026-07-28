// SPDX-License-Identifier: GPL-2.0 OR MIT
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
#include <linux/cacheflush.h>

#include <asm/exception.h>
#include <asm/irq.h>

#include <dt-bindings/interrupt-controller/qcom-sirc.h>

struct msm_sirc {
	void __iomem		*base;
	int					parent_irq;
	struct irq_domain	*domain;
	u32					int_enable;
	u32					wake_enable;
	u32					mask;
	u32					nr_sirc_irqs;
	u32					first_sirc_irq;
};

/* Mask off the given interrupt. Keep the int_enable mask in sync with
 *   the enable reg, so it can be restored after power collapse.
 */
static void sirc_irq_mask(struct irq_data *d)
{
	struct msm_sirc *sirc = irq_data_get_irq_chip_data(d);
	unsigned int mask = BIT(d->hwirq);

	writel(mask, sirc->base + SIRC_INT_ENABLE_CLEAR);
	sirc->int_enable &= ~mask;
}

/* Unmask the given interrupt. Keep the int_enable mask in sync with
 *   the enable reg, so it can be restored after power collapse.
 */
static void sirc_irq_unmask(struct irq_data *d)
{
	struct msm_sirc *sirc = irq_data_get_irq_chip_data(d);
	unsigned int mask = BIT(d->hwirq);

	writel(mask, sirc->base + SIRC_INT_ENABLE_SET);
	sirc->int_enable |= mask;
}

static void sirc_irq_ack(struct irq_data *d)
{
	struct msm_sirc *sirc = irq_data_get_irq_chip_data(d);
	unsigned int mask = BIT(d->hwirq);

	writel(mask, sirc->base + SIRC_INT_CLEAR);
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

	chained_irq_enter(chip, desc);

	status = readl(sirc->base + SIRC_IRQ_STATUS);
	status &= sirc->mask;
	if (status == 0)
		return;

	for (sirq = 0; (sirq < sirc->nr_sirc_irqs); sirq++) {
		if ((status & (1U << sirq)) != 0)
			generic_handle_domain_irq(sirc->domain, sirq);
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
	int irq_base, ret;
	struct msm_sirc *sirc;

	sirc = (struct msm_sirc *)kzalloc_obj(sizeof(*sirc), GFP_KERNEL);
	if (!sirc)
		return -ENOMEM;

	sirc->base = of_iomap(node, 0);
	if (!sirc->base)
		panic("%pOF: unable to map sirc interrupt registers\n", node);

	ret = of_property_read_u32(node, "first-sirc-irq", &sirc->first_sirc_irq);
	if (ret || sirc->first_sirc_irq < 0) {
		pr_err("%pOF: unable to read first-sirc-irq property\n", node);
		return ret;
	}

	ret = of_property_read_u32(node, "nr-sirc-irqs", &sirc->nr_sirc_irqs);
	if (ret || sirc->nr_sirc_irqs < 0) {
		pr_err("%pOF: unable to read nr-sirc-irqs property\n", node);
		return ret;
	}

	ret = of_property_read_u32(node, "sirc-mask", &sirc->mask);
	if (ret) {
		pr_err("%pOF: unable to read sirc-mask property\n", node);
		return ret;
	}

	irq_base = irq_alloc_descs(-1, sirc->first_sirc_irq, sirc->nr_sirc_irqs, 0);
	if (irq_base < 0) {
		pr_warn("Couldn't allocate IRQ numbers\n");
		irq_base = 0;
	}

	sirc->domain = irq_domain_create_legacy(of_fwnode_handle(node), sirc->nr_sirc_irqs, sirc->first_sirc_irq, 0,
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

IRQCHIP_DECLARE(qcom_msm_sirc, "qcom,msm-sirc", msm_init_sirc);