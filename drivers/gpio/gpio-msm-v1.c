// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO/TLMM driver for the Qualcomm MQSD8250-class SoC found in the
 * HTC HD2 ("leo" / "htc,hd2"), and closely related MSM72xx/MSM/QSD8x50
 * parts (bank-based TLMM, pre-msm8x60 single-register-per-gpio TLMM).
 *
 * Ported from the little-kernel / HTC-derived UEFI GPIO driver
 * (Copyright (C) 2008 Google, Inc., Copyright (C) 2011 htc-linux.org,
 *  Copyright (C) 2012 Shantanu Gupta <shans95g@gmail.com>)
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include <linux/mach-msm/msm-proc_comm.h>
static int msm_gpio_tlmm_config_ex(unsigned int gpio, unsigned int dir,
				   unsigned int pull, unsigned int drvstr)
{
	unsigned int data1 = MSM_GPIO_CFG(gpio, 0, dir, pull, drvstr);
	unsigned int data2 = 0;

	return msm_proc_comm(PCOM_RPC_GPIO_TLMM_CONFIG_EX, &data1, &data2);
}

/* Fixed sub-offsets baked into the original GPIOn_REG()/GPIOn_REG_R() macros */
#define MSM_GPIO1_REGION_OFF	0x800
#define MSM_GPIO2_REGION_OFF	0xC00
#define MSM_CFG1_REGION_OFF	0x000
#define MSM_CFG2_REGION_OFF	0x400

enum msm_gpio_region {
	REGION_GPIO1 = 0,
	REGION_GPIO2 = 1,
	REGION_CFG1  = 2,
	REGION_CFG2  = 3,
	REGION_MAX,
};

static const char * const msm_gpio_region_names[REGION_MAX] = {
	[REGION_GPIO1] = "gpio1",
	[REGION_GPIO2] = "gpio2",
	[REGION_CFG1]  = "gpio1-cfg",
	[REGION_CFG2]  = "gpio2-cfg",
};

static const unsigned int msm_gpio_region_fixed_off[REGION_MAX] = {
	[REGION_GPIO1] = MSM_GPIO1_REGION_OFF,
	[REGION_GPIO2] = MSM_GPIO2_REGION_OFF,
	[REGION_CFG1]  = MSM_CFG1_REGION_OFF,
	[REGION_CFG2]  = MSM_CFG2_REGION_OFF,
};

/*
 * One entry per HW register bank. Offsets are exactly the ones from the
 * original GPIO_OUT_n / GPIO_IN_n / ... defines, just annotated with which
 * of the four MMIO regions they live in (see hardware note above: bank 1
 * uniquely lives in the GPIO2/CFG2 window, everything else in GPIO1/CFG1).
 */
struct msm_gpio_bank_desc {
	u32 out_off, in_off, int_status_off, int_clear_off;
	u32 int_en_off, int_edge_off, int_pos_off, oe_off, owner_off;
	u8 io_region;
	u8 owner_region;
	unsigned int start, end;
};

static const struct msm_gpio_bank_desc msm_gpio_banks[] = {
	{ 0x00, 0x50, 0xF0, 0xD0, 0xB0, 0x70, 0x90, 0x20, 0x100,
	  REGION_GPIO1, REGION_CFG1,   0,  15 },
	{ 0x00, 0x20, 0x70, 0x68, 0x60, 0x50, 0x58, 0x08, 0x104,
	  REGION_GPIO2, REGION_CFG2,  16,  42 },
	{ 0x04, 0x54, 0xF4, 0xD4, 0xB4, 0x74, 0x94, 0x24, 0x108,
	  REGION_GPIO1, REGION_CFG1,  43,  67 },
	{ 0x08, 0x58, 0xF8, 0xD8, 0xB8, 0x78, 0x98, 0x28, 0x10c,
	  REGION_GPIO1, REGION_CFG1,  68,  94 },
	{ 0x0C, 0x5C, 0xFC, 0xDC, 0xBC, 0x7C, 0x9C, 0x2C, 0x110,
	  REGION_GPIO1, REGION_CFG1,  95, 103 },
	{ 0x10, 0x60, 0x100, 0xE0, 0xC0, 0x80, 0xA0, 0x30, 0x114,
	  REGION_GPIO1, REGION_CFG1, 104, 121 },
	{ 0x14, 0x64, 0x103, 0xE4, 0xC4, 0x84, 0xA4, 0x34, 0x118,
	  REGION_GPIO1, REGION_CFG1, 122, 152 },
	{ 0x18, 0x68, 0x108, 0xE8, 0xC8, 0x88, 0xA8, 0x38, 0x11c,
	  REGION_GPIO1, REGION_CFG1, 153, 164 },
};

#define MSM_GPIO_NBANKS	ARRAY_SIZE(msm_gpio_banks)
/*
 * Must be a real integer-constant-expression (array indexing into
 * msm_gpio_banks is NOT an ICE in C, even for a static const array - using
 * it here previously blew up as "variably modified type at file scope"
 * once this struct's array members got compiled against a real kernel
 * tree). Keep this literal in sync with the last entry's `.end` in
 * msm_gpio_banks[] above (currently 164 -> 165 lines); the probe-time
 * check below will WARN if it ever drifts.
 */
#define MSM_GPIO_NGPIO	165

struct msm_gpio_chip {
	struct gpio_chip gc;
	struct irq_domain *domain;
	void __iomem *regions[REGION_MAX];
	int parent_irqs[2];
	int n_parent_irqs;
	/* protects read-modify-write register access across banks */
	raw_spinlock_t lock;
	/*
	 * proc_comm TLMM_CONFIG_EX takes pull/drive-strength alongside
	 * direction in one shot, so we cache the last-requested pull and
	 * drive strength per line and re-send them every time direction
	 * changes, instead of losing them on the next direction_input/output
	 * call.
	 */
	u8 pull[MSM_GPIO_NGPIO];
	u8 drvstr[MSM_GPIO_NGPIO];
};

static inline struct msm_gpio_chip *to_msm(struct gpio_chip *gc)
{
	return container_of(gc, struct msm_gpio_chip, gc);
}

static const struct msm_gpio_bank_desc *bank_for_gpio(unsigned int gpio, u32 *bit)
{
	unsigned int i;

	for (i = 0; i < MSM_GPIO_NBANKS; i++) {
		const struct msm_gpio_bank_desc *b = &msm_gpio_banks[i];

		if (gpio >= b->start && gpio <= b->end) {
			*bit = BIT(gpio - b->start);
			return b;
		}
	}
	return NULL;
}

static inline void __iomem *bank_reg(struct msm_gpio_chip *mgc,
				     const struct msm_gpio_bank_desc *b,
				     u8 region, u32 off)
{
	return mgc->regions[region] + msm_gpio_region_fixed_off[region] + off;
}

/* ---------------------------------------------------------------------- */
/* gpio_chip ops                                                          */
/* ---------------------------------------------------------------------- */

static int msm_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct msm_gpio_chip *mgc = to_msm(gc);
	const struct msm_gpio_bank_desc *b;
	void __iomem *oe;
	u32 bit;

	b = bank_for_gpio(offset, &bit);
	if (!b)
		return -EINVAL;

	oe = bank_reg(mgc, b, b->io_region, b->oe_off);
	return (readl(oe) & bit) ? GPIO_LINE_DIRECTION_OUT
				  : GPIO_LINE_DIRECTION_IN;
}

static void msm_gpio_set_oe(struct msm_gpio_chip *mgc,
			    const struct msm_gpio_bank_desc *b, u32 bit,
			    bool output)
{
	void __iomem *oe = bank_reg(mgc, b, b->io_region, b->oe_off);
	unsigned long flags;
	u32 v;

	raw_spin_lock_irqsave(&mgc->lock, flags);
	v = readl(oe);
	if (output)
		v |= bit;
	else
		v &= ~bit;
	writel(v, oe);
	raw_spin_unlock_irqrestore(&mgc->lock, flags);
}

static int msm_gpio_direction_input(struct gpio_chip *gc, unsigned int offset)
{
	struct msm_gpio_chip *mgc = to_msm(gc);
	const struct msm_gpio_bank_desc *b;
	u32 bit;
	int ret;

	b = bank_for_gpio(offset, &bit);
	if (!b)
		return -EINVAL;

	ret = msm_gpio_tlmm_config_ex(offset, MSM_GPIO_CFG_INPUT,
				      mgc->pull[offset], mgc->drvstr[offset]);
	if (ret)
		dev_warn(gc->parent, "gpio %u: TLMM_CONFIG_EX (input) failed: %d\n",
			 offset, ret);

	msm_gpio_set_oe(mgc, b, bit, false);
	return 0;
}

static int msm_gpio_direction_output(struct gpio_chip *gc, unsigned int offset,
				     int value)
{
	struct msm_gpio_chip *mgc = to_msm(gc);
	const struct msm_gpio_bank_desc *b;
	void __iomem *out;
	unsigned long flags;
	u32 bit, v;
	int ret;

	b = bank_for_gpio(offset, &bit);
	if (!b)
		return -EINVAL;

	ret = msm_gpio_tlmm_config_ex(offset, MSM_GPIO_CFG_OUTPUT,
				      mgc->pull[offset], mgc->drvstr[offset]);
	if (ret)
		dev_warn(gc->parent, "gpio %u: TLMM_CONFIG_EX (output) failed: %d\n",
			 offset, ret);

	out = bank_reg(mgc, b, b->io_region, b->out_off);

	raw_spin_lock_irqsave(&mgc->lock, flags);
	v = readl(out);
	if (value)
		v |= bit;
	else
		v &= ~bit;
	writel(v, out);
	raw_spin_unlock_irqrestore(&mgc->lock, flags);

	msm_gpio_set_oe(mgc, b, bit, true);
	return 0;
}

/*
 * Pull-up/down and drive strength aren't backed by any register this
 * driver otherwise touches - they only exist on the proc_comm side, so
 * this is a pure TLMM_CONFIG_EX round trip using whatever direction the
 * line is currently in.
 */
static int msm_gpio_set_config(struct gpio_chip *gc, unsigned int offset,
			       unsigned long config)
{
	struct msm_gpio_chip *mgc = to_msm(gc);
	enum pin_config_param param = pinconf_to_config_param(config);
	u32 arg = pinconf_to_config_argument(config);
	u8 pull, drvstr;
	int dir;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		pull = MSM_GPIO_CFG_NO_PULL;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		pull = arg ? MSM_GPIO_CFG_PULL_DOWN : MSM_GPIO_CFG_NO_PULL;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		pull = arg ? MSM_GPIO_CFG_PULL_UP : MSM_GPIO_CFG_NO_PULL;
		break;
	case PIN_CONFIG_BIAS_BUS_HOLD:
		pull = MSM_GPIO_CFG_KEEPER;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		/* arg is mA; clamp into the 2-16mA/2mA-step range this HW supports */
		if (arg < 2)
			arg = 2;
		if (arg > 16)
			arg = 16;
		mgc->drvstr[offset] = (arg / 2) - 1;
		pull = mgc->pull[offset];
		goto apply;
	default:
		return -ENOTSUPP;
	}

	mgc->pull[offset] = pull;
apply:
	drvstr = mgc->drvstr[offset];
	dir = msm_gpio_get_direction(gc, offset);
	if (dir < 0)
		return dir;

	return msm_gpio_tlmm_config_ex(offset,
				       dir == GPIO_LINE_DIRECTION_OUT ?
				       MSM_GPIO_CFG_OUTPUT : MSM_GPIO_CFG_INPUT,
				       pull, drvstr);
}

static int msm_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct msm_gpio_chip *mgc = to_msm(gc);
	const struct msm_gpio_bank_desc *b;
	void __iomem *in;
	u32 bit;

	b = bank_for_gpio(offset, &bit);
	if (!b)
		return -EINVAL;

	in = bank_reg(mgc, b, b->io_region, b->in_off);
	return !!(readl(in) & bit);
}

static int msm_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct msm_gpio_chip *mgc = to_msm(gc);
	const struct msm_gpio_bank_desc *b;
	void __iomem *out;
	unsigned long flags;
	u32 bit, v;

	b = bank_for_gpio(offset, &bit);
	if (!b)
		return -EINVAL;

	out = bank_reg(mgc, b, b->io_region, b->out_off);

	raw_spin_lock_irqsave(&mgc->lock, flags);
	v = readl(out);
	if (value)
		v |= bit;
	else
		v &= ~bit;
	writel(v, out);
	raw_spin_unlock_irqrestore(&mgc->lock, flags);

	return 0;
}

/* ---------------------------------------------------------------------- */
/* irqchip ops                                                            */
/* ---------------------------------------------------------------------- */

/*
 * "Both edge" detect on this hardware isn't a real trigger mode - it is
 * emulated by watching the current level and re-polarizing INT_POS after
 * every edge, same trick the original bootloader driver used.
 */
static void msm_gpio_update_both_edge_detect(struct msm_gpio_chip *mgc,
					     const struct msm_gpio_bank_desc *b,
					     u32 bit)
{
	void __iomem *in  = bank_reg(mgc, b, b->io_region, b->in_off);
	void __iomem *pos = bank_reg(mgc, b, b->io_region, b->int_pos_off);
	void __iomem *st  = bank_reg(mgc, b, b->io_region, b->int_status_off);
	int loop = 100;
	u32 val, val2, p;

	do {
		val = readl(in);
		p = readl(pos);
		if (val & bit)
			p &= ~bit;
		else
			p |= bit;
		writel(p, pos);
		val2 = readl(in);
		if (((val ^ val2) & bit & ~readl(st)) == 0)
			return;
	} while (--loop > 0);
}

static void msm_gpio_irq_mask(struct irq_data *d)
{
	struct msm_gpio_chip *mgc = irq_data_get_irq_chip_data(d);
	const struct msm_gpio_bank_desc *b;
	void __iomem *en;
	unsigned long flags;
	u32 bit, v;

	b = bank_for_gpio(d->hwirq, &bit);
	if (!b)
		return;

	en = bank_reg(mgc, b, b->io_region, b->int_en_off);
	raw_spin_lock_irqsave(&mgc->lock, flags);
	v = readl(en) & ~bit;
	writel(v, en);
	raw_spin_unlock_irqrestore(&mgc->lock, flags);
}

static void msm_gpio_irq_unmask(struct irq_data *d)
{
	struct msm_gpio_chip *mgc = irq_data_get_irq_chip_data(d);
	const struct msm_gpio_bank_desc *b;
	void __iomem *en;
	unsigned long flags;
	u32 bit, v;

	b = bank_for_gpio(d->hwirq, &bit);
	if (!b)
		return;

	en = bank_reg(mgc, b, b->io_region, b->int_en_off);
	raw_spin_lock_irqsave(&mgc->lock, flags);
	v = readl(en) | bit;
	writel(v, en);
	raw_spin_unlock_irqrestore(&mgc->lock, flags);
}

static void msm_gpio_irq_ack(struct irq_data *d)
{
	struct msm_gpio_chip *mgc = irq_data_get_irq_chip_data(d);
	const struct msm_gpio_bank_desc *b;
	void __iomem *clr;
	u32 bit;

	b = bank_for_gpio(d->hwirq, &bit);
	if (!b)
		return;

	clr = bank_reg(mgc, b, b->io_region, b->int_clear_off);
	writel(bit, clr);
	msm_gpio_update_both_edge_detect(mgc, b, bit);
}

static int msm_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct msm_gpio_chip *mgc = irq_data_get_irq_chip_data(d);
	const struct msm_gpio_bank_desc *b;
	void __iomem *edge, *pos;
	unsigned long flags;
	u32 bit, e, p;

	b = bank_for_gpio(d->hwirq, &bit);
	if (!b)
		return -EINVAL;

	/* Only edge triggering is wired up on this SoC generation */
	if (type & IRQ_TYPE_LEVEL_MASK)
		return -EINVAL;

	edge = bank_reg(mgc, b, b->io_region, b->int_edge_off);
	pos  = bank_reg(mgc, b, b->io_region, b->int_pos_off);

	raw_spin_lock_irqsave(&mgc->lock, flags);
	e = readl(edge) | bit;		/* 1 = edge-triggered */
	writel(e, edge);

	p = readl(pos);
	if (type == IRQ_TYPE_EDGE_RISING)
		p |= bit;
	else if (type == IRQ_TYPE_EDGE_FALLING)
		p &= ~bit;
	/* EDGE_BOTH: leave as-is, the ack-time "both edge" trick re-polarizes */
	writel(p, pos);
	raw_spin_unlock_irqrestore(&mgc->lock, flags);

	if (type == IRQ_TYPE_EDGE_BOTH)
		msm_gpio_update_both_edge_detect(mgc, b, bit);

	if (type == IRQ_TYPE_EDGE_RISING)
		irq_set_handler_locked(d, handle_edge_irq);
	else
		irq_set_handler_locked(d, handle_edge_irq);

	return 0;
}

static int msm_gpio_to_irq(struct gpio_chip *gc, unsigned int offset)
{
	struct msm_gpio_chip *mgc = to_msm(gc);

	return irq_create_mapping(mgc->domain, offset);
}

static struct irq_chip msm_gpio_irq_chip = {
	.name		= "qsd8250-gpio",
	.irq_mask	= msm_gpio_irq_mask,
	.irq_unmask	= msm_gpio_irq_unmask,
	.irq_ack	= msm_gpio_irq_ack,
	.irq_set_type	= msm_gpio_irq_set_type,
	.flags		= IRQCHIP_SKIP_SET_WAKE,
};

/* Demux handler shared by every parent interrupt line (GROUP1 / GROUP2) */
static irqreturn_t msm_gpio_irq_handler(int parent_irq, void *data)
{
	struct msm_gpio_chip *mgc = data;
	unsigned int i;
	bool handled = false;

	for (i = 0; i < MSM_GPIO_NBANKS; i++) {
		const struct msm_gpio_bank_desc *b = &msm_gpio_banks[i];
		void __iomem *st_reg = bank_reg(mgc, b, b->io_region, b->int_status_off);
		void __iomem *en_reg = bank_reg(mgc, b, b->io_region, b->int_en_off);
		u32 pending, en, j;

		en = readl(en_reg);
		pending = readl(st_reg) & en;
		if (!pending)
			continue;

		for (j = 0; j < 32 && (b->start + j) <= b->end; j++) {
			if (pending & BIT(j)) {
				generic_handle_domain_irq(mgc->domain, b->start + j);
				handled = true;
			}
		}
	}

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static int msm_gpio_irq_domain_map(struct irq_domain *d, unsigned int irq,
				   irq_hw_number_t hwirq)
{
	struct msm_gpio_chip *mgc = d->host_data;

	irq_set_chip_data(irq, mgc);
	irq_set_chip_and_handler(irq, &msm_gpio_irq_chip, handle_edge_irq);
	irq_set_noprobe(irq);
	return 0;
}

static const struct irq_domain_ops msm_gpio_irq_domain_ops = {
	.map = msm_gpio_irq_domain_map,
	.xlate = irq_domain_xlate_twocell,
};

/* ---------------------------------------------------------------------- */
/* probe                                                                  */
/* ---------------------------------------------------------------------- */

static int msm_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct msm_gpio_chip *mgc;
	unsigned int i;
	int ret;

	mgc = devm_kzalloc(dev, sizeof(*mgc), GFP_KERNEL);
	if (!mgc)
		return -ENOMEM;

	if (msm_gpio_banks[MSM_GPIO_NBANKS - 1].end + 1 != MSM_GPIO_NGPIO)
		dev_warn(dev,
			"MSM_GPIO_NGPIO (%u) out of sync with bank table (%u) - fix the #define\n",
			MSM_GPIO_NGPIO, msm_gpio_banks[MSM_GPIO_NBANKS - 1].end + 1);

	raw_spin_lock_init(&mgc->lock);

	for (i = 0; i < REGION_MAX; i++) {
		mgc->regions[i] = devm_platform_ioremap_resource_byname(pdev,
							msm_gpio_region_names[i]);
		if (IS_ERR(mgc->regions[i]))
			return dev_err_probe(dev, PTR_ERR(mgc->regions[i]),
					     "failed to map %s region\n",
					     msm_gpio_region_names[i]);
	}

	/* Mask + clear everything before anyone can see a stale IRQ */
	for (i = 0; i < MSM_GPIO_NBANKS; i++) {
		const struct msm_gpio_bank_desc *b = &msm_gpio_banks[i];

		writel(0, bank_reg(mgc, b, b->io_region, b->int_en_off));
		writel(~0U, bank_reg(mgc, b, b->io_region, b->int_clear_off));
	}

	mgc->gc.label = "qsd8250-gpio";
	mgc->gc.parent = dev;
	mgc->gc.owner = THIS_MODULE;
	mgc->gc.request = gpiochip_generic_request;
	mgc->gc.free = gpiochip_generic_free;
	mgc->gc.get_direction = msm_gpio_get_direction;
	mgc->gc.direction_input = msm_gpio_direction_input;
	mgc->gc.direction_output = msm_gpio_direction_output;
	mgc->gc.get = msm_gpio_get;
	mgc->gc.set = msm_gpio_set;
	mgc->gc.set_config = msm_gpio_set_config;
	mgc->gc.to_irq = msm_gpio_to_irq;
	mgc->gc.base = -1;
	mgc->gc.ngpio = MSM_GPIO_NGPIO;
	/* proc_comm is an SMD RPC round trip to the modem CPU - it can block */
	mgc->gc.can_sleep = true;

	for (i = 0; i < MSM_GPIO_NGPIO; i++) {
		mgc->pull[i] = MSM_GPIO_CFG_NO_PULL;
		mgc->drvstr[i] = MSM_GPIO_CFG_8MA;
	}

	mgc->domain = irq_domain_create_linear(dev_fwnode(dev), MSM_GPIO_NGPIO,
					       &msm_gpio_irq_domain_ops, mgc);
	if (!mgc->domain)
		return dev_err_probe(dev, -ENOMEM, "failed to add irq domain\n");

	ret = devm_gpiochip_add_data(dev, &mgc->gc, mgc);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register gpiochip\n");

	mgc->n_parent_irqs = platform_irq_count(pdev);
	if (mgc->n_parent_irqs <= 0 || mgc->n_parent_irqs > ARRAY_SIZE(mgc->parent_irqs))
		return dev_err_probe(dev, -EINVAL,
				     "expected 1-2 parent interrupts, got %d\n",
				     mgc->n_parent_irqs);

	for (i = 0; i < mgc->n_parent_irqs; i++) {
		mgc->parent_irqs[i] = platform_get_irq(pdev, i);
		if (mgc->parent_irqs[i] < 0)
			return mgc->parent_irqs[i];

		ret = devm_request_irq(dev, mgc->parent_irqs[i],
					msm_gpio_irq_handler, IRQF_NO_SUSPEND,
					dev_name(dev), mgc);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to request irq %d\n",
					     mgc->parent_irqs[i]);
	}

	platform_set_drvdata(pdev, mgc);
	dev_info(dev, "qsd8250 TLMM GPIO controller, %u lines, %d parent irqs\n",
		 MSM_GPIO_NGPIO, mgc->n_parent_irqs);
	return 0;
}

static const struct of_device_id msm_gpio_of_match[] = {
	{ .compatible = "qcom,qsd8250-gpio" },
	{ }
};
MODULE_DEVICE_TABLE(of, msm_gpio_of_match);

static struct platform_driver msm_gpio_driver = {
	.probe = msm_gpio_probe,
	.driver = {
		.name = "gpio-qsd8250",
		.of_match_table = msm_gpio_of_match,
	},
};
module_platform_driver(msm_gpio_driver);

MODULE_DESCRIPTION("Qualcomm QSD8250 TLMM GPIO driver");
MODULE_LICENSE("GPL v2");