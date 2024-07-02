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

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/cacheflush.h>
#include <asm/exception.h>
#include <asm/irq.h>

/*
 * Secondary interrupt controller interrupts
 */

/* reference for now

#define INT_UART1                     (FIRST_SIRC_IRQ + 0)
#define INT_UART2                     (FIRST_SIRC_IRQ + 1)
#define INT_UART3                     (FIRST_SIRC_IRQ + 2)
#define INT_UART1_RX                  (FIRST_SIRC_IRQ + 3)
#define INT_UART2_RX                  (FIRST_SIRC_IRQ + 4)
#define INT_UART3_RX                  (FIRST_SIRC_IRQ + 5)
#define INT_SPI_INPUT                 (FIRST_SIRC_IRQ + 6)
#define INT_SPI_OUTPUT                (FIRST_SIRC_IRQ + 7)
#define INT_SPI_ERROR                 (FIRST_SIRC_IRQ + 8)
#define INT_GPIO_GROUP1               (FIRST_SIRC_IRQ + 9)
#define INT_GPIO_GROUP2               (FIRST_SIRC_IRQ + 10)
#define INT_GPIO_GROUP1_SECURE        (FIRST_SIRC_IRQ + 11)
#define INT_GPIO_GROUP2_SECURE        (FIRST_SIRC_IRQ + 12)
#define INT_AVS_SVIC                  (FIRST_SIRC_IRQ + 13)
#define INT_AVS_REQ_UP                (FIRST_SIRC_IRQ + 14)
#define INT_AVS_REQ_DOWN              (FIRST_SIRC_IRQ + 15)
#define INT_PBUS_ERR                  (FIRST_SIRC_IRQ + 16)
#define INT_AXI_ERR                   (FIRST_SIRC_IRQ + 17)
#define INT_SMI_ERR                   (FIRST_SIRC_IRQ + 18)
#define INT_EBI1_ERR                  (FIRST_SIRC_IRQ + 19)
#define INT_IMEM_ERR                  (FIRST_SIRC_IRQ + 20)
#define INT_TEMP_SENSOR               (FIRST_SIRC_IRQ + 21)
#define INT_TV_ENC                    (FIRST_SIRC_IRQ + 22)
#define INT_GRP2D                     (FIRST_SIRC_IRQ + 23)
#define INT_GSBI_QUP                  (FIRST_SIRC_IRQ + 24)
#define INT_SC_ACG                    (FIRST_SIRC_IRQ + 25)
#define INT_WDT0                      (FIRST_SIRC_IRQ + 26)
#define INT_WDT1                      (FIRST_SIRC_IRQ + 27)*/

#define NR_SIRC_IRQS                  23
#define SIRC_MASK                     0x007FFFFF

#define FIRST_SIRC_IRQ                64
#define LAST_SIRC_IRQ                 (FIRST_SIRC_IRQ + NR_SIRC_IRQS - 1)

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

void __iomem *sirc_base;
static struct irq_domain *domain;
int parent_irq; //9

/* Mask off the given interrupt. Keep the int_enable mask in sync with
   the enable reg, so it can be restored after power collapse. */
static void sirc_irq_mask(struct irq_data *d)
{
	unsigned int mask;

	mask = 1 << (d->irq - FIRST_SIRC_IRQ);
	writel(mask, sirc_base + SIRC_INT_ENABLE_CLEAR);
	//int_enable &= ~mask;
	return;
}

/* Unmask the given interrupt. Keep the int_enable mask in sync with
   the enable reg, so it can be restored after power collapse. */
static void sirc_irq_unmask(struct irq_data *d)
{
	unsigned int mask;

	mask = 1 << (d->irq - FIRST_SIRC_IRQ);
	writel(mask, sirc_base + SIRC_INT_ENABLE_SET);
	//int_enable |= mask;
	return;
}

static void sirc_irq_ack(struct irq_data *d)
{
	unsigned int mask;

	mask = 1 << (d->irq - FIRST_SIRC_IRQ);
	writel(mask, sirc_base + SIRC_INT_CLEAR);
	return;
}

static int sirc_irq_set_wake(struct irq_data *d, unsigned int on)
{
	unsigned int mask;

	/* Used to set the interrupt enable mask during power collapse. */
	mask = 1 << (d->irq - FIRST_SIRC_IRQ);
	/*if (on)
		wake_enable |= mask;
	else
		wake_enable &= ~mask;*/

	return 0;
}

static int sirc_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	unsigned int mask;
	unsigned int val;

	mask = 1 << (d->irq - FIRST_SIRC_IRQ);
	val = readl(sirc_base + SIRC_INT_POLARITY);

	if (flow_type & (IRQF_TRIGGER_LOW | IRQF_TRIGGER_FALLING))
		val |= mask;
	else
		val &= ~mask;

	writel(val, sirc_base + SIRC_INT_POLARITY);

	val = readl(sirc_base + SIRC_INT_TYPE);
	if (flow_type & (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING)) {
		val |= mask;
		irq_set_handler_locked(d, handle_edge_irq);
	} else {
		val &= ~mask;
		irq_set_handler_locked(d, handle_level_irq);
	}

	writel(val, sirc_base + SIRC_INT_TYPE);

	return 0;
}

/* Finds the pending interrupt on the passed cascade irq and redrives it */
static void sirc_irq_handler(struct irq_desc *desc)//unsigned int irq, struct irq_desc *desc)
{
	//unsigned int reg = 0;
	unsigned int sirq;
	unsigned int status;

	/*while ((reg < NUM_SIRC_REGS) && (irq != parent_irq))
		reg++;*/

	status = readl(sirc_base + SIRC_IRQ_STATUS);
	status &= SIRC_MASK;
	if (status == 0)
		return;

	for (sirq = 0;
	     (sirq < NR_SIRC_IRQS) && ((status & (1U << sirq)) == 0);
	     sirq++)
		;
	generic_handle_irq(sirq+FIRST_SIRC_IRQ);

	desc->irq_data.chip->irq_ack(&desc->irq_data);
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
	irq_set_chip_data(irq, sirc_base);

	return 0;
}

static const struct irq_domain_ops msm_sirc_irqchip_intc_ops = {
	.xlate = irq_domain_xlate_onetwocell,
	.map = msm_sirc_map,
};

static int __init msm_init_sirc(struct device_node *node, struct device_node *parent)
{
	int i, irq_base;

	sirc_base = of_iomap(node, 0);
    if (!sirc_base){
		panic("%pOF: unable to map sirc interrupt registers\n", node);
	}

    irq_base = irq_alloc_descs(-1, FIRST_SIRC_IRQ, NR_SIRC_IRQS, 0);
	if (irq_base < 0) {
		pr_warn("Couldn't allocate IRQ numbers\n");
        irq_base = 0;
	}

    domain = irq_domain_add_legacy(node, NR_SIRC_IRQS,
					       irq_base, FIRST_SIRC_IRQ,
					       &msm_sirc_irqchip_intc_ops, NULL);
	if (!domain)
		panic("Unable to add SIRC IRQ domain\n");

	/* Map the parent interrupt for the chained handler */
	parent_irq = irq_of_parse_and_map(node, 0);
	if (parent_irq <= 0) {
		pr_err("%pOF: unable to parse sirc irq\n", node);
		return -EINVAL;
	}
	//domain_ops = &

	/*for (i = FIRST_SIRC_IRQ; i < LAST_SIRC_IRQ; i++) {
		irq_set_chip_and_handler(i, &sirc_irq_chip, handle_edge_irq);
		//set_irq_flags(i, IRQF_VALID); TODO: domains
	}*/

	for (i = 0; i < NUM_SIRC_REGS; i++) {
		irq_set_chained_handler(parent_irq,
					sirc_irq_handler);
		irq_set_irq_wake(parent_irq, 1);
	}

	return 0;
}

IRQCHIP_DECLARE(arm_msm_sirc, "arm,msm-sirc", msm_init_sirc);