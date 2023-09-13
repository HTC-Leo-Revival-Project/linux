static void vic_mask_irq(struct irq_data *d)
{
	void __iomem *reg = VIC_INT_TO_REG_ADDR(VIC_INT_ENCLEAR0, d->irq);
	unsigned index = VIC_INT_TO_REG_INDEX(d->irq);
	uint32_t mask = 1UL << (d->irq & 31);
	int smsm_irq = msm_irq_to_smsm[d->irq];

	msm_irq_shadow_reg[index].int_en[0] &= ~mask;
	writel(mask, reg);
	mb();
	if (smsm_irq == 0)
		msm_irq_idle_disable[index] &= ~mask;
	else {
		mask = 1UL << (smsm_irq - 1);
		msm_irq_smsm_wake_enable[0] &= ~mask;
	}
}


static void vic_unmask_irq(struct irq_data *d)
{
	void __iomem *reg = VIC_INT_TO_REG_ADDR(VIC_INT_ENSET0, d->irq);
	unsigned index = VIC_INT_TO_REG_INDEX(d->irq);
	uint32_t mask = 1UL << (d->irq & 31);
	int smsm_irq = msm_irq_to_smsm[d->irq];

	msm_irq_shadow_reg[index].int_en[0] |= mask;
	writel(mask, reg);
	mb();

	if (smsm_irq == 0)
		msm_irq_idle_disable[index] |= mask;
	else {
		mask = 1UL << (smsm_irq - 1);
		msm_irq_smsm_wake_enable[0] |= mask;
	}
}

static void __init __vic_init(void __iomem *base, int parent_irq, int irq_start,
			      u32 vic_sources, u32 resume_sources,
			      struct device_node *node)
{

	// /* Disable all interrupts initially. */
	// vic_disable(base);

	// /* Make sure we clear all existing interrupts */
	// vic_clear_interrupts(base);

	// vic_init2(base);

	// vic_register(base, parent_irq, irq_start, vic_sources, resume_sources, node);
}

/**
 * vic_init() - initialise a vectored interrupt controller
 * @base: iomem base address
 * @irq_start: starting interrupt number, must be muliple of 32
 * @vic_sources: bitmask of interrupt sources to allow
 * @resume_sources: bitmask of interrupt sources to allow for resume
 */
void __init vic_init(void __iomem *base, unsigned int irq_start,
		     u32 vic_sources, u32 resume_sources)
{
	// __vic_init(base, 0, irq_start, vic_sources, resume_sources, NULL);
}


static int __init vic_of_init(struct device_node *node,
			      struct device_node *parent)
{


	return 0;
}
IRQCHIP_DECLARE(arm_pl190_vic, "arm,msm-vic", vic_of_init);