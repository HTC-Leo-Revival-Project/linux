// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <linux/mach-msm/msm-proc_comm.h>

/* ---------------------------------------------------------------------- */
/* proc_comm TLMM_CONFIG_EX word + RPC helper                              */
/* ---------------------------------------------------------------------- */

static int qsd8250_tlmm_config_ex(unsigned int gpio, unsigned int func,
				   unsigned int dir, unsigned int pull,
				   unsigned int drvstr)
{
	unsigned int data1 = PCOM_GPIO_CFG(gpio, func, dir, pull, drvstr);
	unsigned int data2 = 0;

	return msm_proc_comm(PCOM_RPC_GPIO_TLMM_CONFIG_EX, &data1, &data2);
}

#define QSD8250_NFUNCS		16

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
#define MSM_GPIO_NGPIO	165

struct msm_pinctrl {
	struct device *dev;
	struct gpio_chip gc;
	struct pinctrl_dev *pctldev;
	struct pinctrl_desc desc;
	struct irq_domain *domain;
	void __iomem *regions[REGION_MAX];
	int parent_irqs[2];
	int n_parent_irqs;
	raw_spinlock_t lock;

	u8 func[MSM_GPIO_NGPIO];
	u8 pull[MSM_GPIO_NGPIO];
	u8 drvstr[MSM_GPIO_NGPIO];

	struct pinctrl_pin_desc pins[MSM_GPIO_NGPIO];
	const char *pin_names[MSM_GPIO_NGPIO];
	const char *group_names[MSM_GPIO_NGPIO];
	const char *func_names[QSD8250_NFUNCS];
};

static inline struct msm_pinctrl *gc_to_pc(struct gpio_chip *gc)
{
	return container_of(gc, struct msm_pinctrl, gc);
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

static inline void __iomem *bank_reg(struct msm_pinctrl *pc,
				      const struct msm_gpio_bank_desc *b,
				      u8 region, u32 off)
{
	return pc->regions[region] + msm_gpio_region_fixed_off[region] + off;
}

static int qsd8250_apply_pin_config(struct msm_pinctrl *pc, unsigned int gpio,
				     unsigned int dir)
{
	int ret = qsd8250_tlmm_config_ex(gpio, pc->func[gpio], dir,
					  pc->pull[gpio], pc->drvstr[gpio]);
	if (ret)
		dev_warn(pc->dev, "gpio %u: TLMM_CONFIG_EX failed: %d\n", gpio, ret);
	return ret;
}

/* ---------------------------------------------------------------------- */
/* gpio_chip ops                                                          */
/* ---------------------------------------------------------------------- */

static int qsd8250_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct msm_pinctrl *pc = gc_to_pc(gc);
	const struct msm_gpio_bank_desc *b;
	void __iomem *oe;
	u32 bit;

	b = bank_for_gpio(offset, &bit);
	if (!b)
		return -EINVAL;

	oe = bank_reg(pc, b, b->io_region, b->oe_off);
	return (readl(oe) & bit) ? GPIO_LINE_DIRECTION_OUT
				  : GPIO_LINE_DIRECTION_IN;
}

static void qsd8250_gpio_set_oe(struct msm_pinctrl *pc,
				 const struct msm_gpio_bank_desc *b, u32 bit,
				 bool output)
{
	void __iomem *oe = bank_reg(pc, b, b->io_region, b->oe_off);
	unsigned long flags;
	u32 v;

	raw_spin_lock_irqsave(&pc->lock, flags);
	v = readl(oe);
	if (output)
		v |= bit;
	else
		v &= ~bit;
	writel(v, oe);
	raw_spin_unlock_irqrestore(&pc->lock, flags);
}

static int qsd8250_gpio_direction_input(struct gpio_chip *gc, unsigned int offset)
{
	struct msm_pinctrl *pc = gc_to_pc(gc);
	const struct msm_gpio_bank_desc *b;
	u32 bit;

	b = bank_for_gpio(offset, &bit);
	if (!b)
		return -EINVAL;

	qsd8250_apply_pin_config(pc, offset, PCOM_GPIO_CFG_INPUT);
	qsd8250_gpio_set_oe(pc, b, bit, false);
	return 0;
}

static int qsd8250_gpio_direction_output(struct gpio_chip *gc, unsigned int offset,
					  int value)
{
	struct msm_pinctrl *pc = gc_to_pc(gc);
	const struct msm_gpio_bank_desc *b;
	void __iomem *out;
	unsigned long flags;
	u32 bit, v;

	b = bank_for_gpio(offset, &bit);
	if (!b)
		return -EINVAL;

	qsd8250_apply_pin_config(pc, offset, PCOM_GPIO_CFG_OUTPUT);

	out = bank_reg(pc, b, b->io_region, b->out_off);
	raw_spin_lock_irqsave(&pc->lock, flags);
	v = readl(out);
	if (value)
		v |= bit;
	else
		v &= ~bit;
	writel(v, out);
	raw_spin_unlock_irqrestore(&pc->lock, flags);

	qsd8250_gpio_set_oe(pc, b, bit, true);
	return 0;
}

static int qsd8250_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct msm_pinctrl *pc = gc_to_pc(gc);
	const struct msm_gpio_bank_desc *b;
	void __iomem *in;
	u32 bit;

	b = bank_for_gpio(offset, &bit);
	if (!b)
		return -EINVAL;

	in = bank_reg(pc, b, b->io_region, b->in_off);
	return !!(readl(in) & bit);
}

static int qsd8250_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct msm_pinctrl *pc = gc_to_pc(gc);
	const struct msm_gpio_bank_desc *b;
	void __iomem *out;
	unsigned long flags;
	u32 bit, v;

	b = bank_for_gpio(offset, &bit);
	if (!b)
		return -EINVAL;

	out = bank_reg(pc, b, b->io_region, b->out_off);
	raw_spin_lock_irqsave(&pc->lock, flags);
	v = readl(out);
	if (value)
		v |= bit;
	else
		v &= ~bit;
	writel(v, out);
	raw_spin_unlock_irqrestore(&pc->lock, flags);

	return 0;
}

/* ---------------------------------------------------------------------- */
/* irqchip ops                                                            */
/* ---------------------------------------------------------------------- */

static void qsd8250_update_both_edge_detect(struct msm_pinctrl *pc,
					     const struct msm_gpio_bank_desc *b,
					     u32 bit)
{
	void __iomem *in  = bank_reg(pc, b, b->io_region, b->in_off);
	void __iomem *pos = bank_reg(pc, b, b->io_region, b->int_pos_off);
	void __iomem *st  = bank_reg(pc, b, b->io_region, b->int_status_off);
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

static void qsd8250_irq_mask(struct irq_data *d)
{
	struct msm_pinctrl *pc = irq_data_get_irq_chip_data(d);
	const struct msm_gpio_bank_desc *b;
	void __iomem *en;
	unsigned long flags;
	u32 bit, v;

	b = bank_for_gpio(d->hwirq, &bit);
	if (!b)
		return;

	en = bank_reg(pc, b, b->io_region, b->int_en_off);
	raw_spin_lock_irqsave(&pc->lock, flags);
	v = readl(en) & ~bit;
	writel(v, en);
	raw_spin_unlock_irqrestore(&pc->lock, flags);
}

static void qsd8250_irq_unmask(struct irq_data *d)
{
	struct msm_pinctrl *pc = irq_data_get_irq_chip_data(d);
	const struct msm_gpio_bank_desc *b;
	void __iomem *en;
	unsigned long flags;
	u32 bit, v;

	b = bank_for_gpio(d->hwirq, &bit);
	if (!b)
		return;

	en = bank_reg(pc, b, b->io_region, b->int_en_off);
	raw_spin_lock_irqsave(&pc->lock, flags);
	v = readl(en) | bit;
	writel(v, en);
	raw_spin_unlock_irqrestore(&pc->lock, flags);
}

static void qsd8250_irq_ack(struct irq_data *d)
{
	struct msm_pinctrl *pc = irq_data_get_irq_chip_data(d);
	const struct msm_gpio_bank_desc *b;
	void __iomem *clr;
	u32 bit;

	b = bank_for_gpio(d->hwirq, &bit);
	if (!b)
		return;

	clr = bank_reg(pc, b, b->io_region, b->int_clear_off);
	writel(bit, clr);
	qsd8250_update_both_edge_detect(pc, b, bit);
}

static int qsd8250_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct msm_pinctrl *pc = irq_data_get_irq_chip_data(d);
	const struct msm_gpio_bank_desc *b;
	void __iomem *edge, *pos;
	unsigned long flags;
	u32 bit, e, p;

	b = bank_for_gpio(d->hwirq, &bit);
	if (!b)
		return -EINVAL;

	if (type & IRQ_TYPE_LEVEL_MASK)
		return -EINVAL;

	edge = bank_reg(pc, b, b->io_region, b->int_edge_off);
	pos  = bank_reg(pc, b, b->io_region, b->int_pos_off);

	raw_spin_lock_irqsave(&pc->lock, flags);
	e = readl(edge) | bit;
	writel(e, edge);

	p = readl(pos);
	if (type == IRQ_TYPE_EDGE_RISING)
		p |= bit;
	else if (type == IRQ_TYPE_EDGE_FALLING)
		p &= ~bit;
	writel(p, pos);
	raw_spin_unlock_irqrestore(&pc->lock, flags);

	if (type == IRQ_TYPE_EDGE_BOTH)
		qsd8250_update_both_edge_detect(pc, b, bit);

	irq_set_handler_locked(d, handle_edge_irq);
	return 0;
}

static int qsd8250_gpio_to_irq(struct gpio_chip *gc, unsigned int offset)
{
	struct msm_pinctrl *pc = gc_to_pc(gc);

	return irq_create_mapping(pc->domain, offset);
}

static struct irq_chip qsd8250_irq_chip = {
	.name		= "qsd8250-gpio",
	.irq_mask	= qsd8250_irq_mask,
	.irq_unmask	= qsd8250_irq_unmask,
	.irq_ack	= qsd8250_irq_ack,
	.irq_set_type	= qsd8250_irq_set_type,
	.flags		= IRQCHIP_SKIP_SET_WAKE,
};

static irqreturn_t qsd8250_irq_handler(int parent_irq, void *data)
{
	struct msm_pinctrl *pc = data;
	unsigned int i;
	bool handled = false;

	for (i = 0; i < MSM_GPIO_NBANKS; i++) {
		const struct msm_gpio_bank_desc *b = &msm_gpio_banks[i];
		void __iomem *st_reg = bank_reg(pc, b, b->io_region, b->int_status_off);
		void __iomem *en_reg = bank_reg(pc, b, b->io_region, b->int_en_off);
		u32 pending, en, j;

		en = readl(en_reg);
		pending = readl(st_reg) & en;
		if (!pending)
			continue;

		for (j = 0; j < 32 && (b->start + j) <= b->end; j++) {
			if (pending & BIT(j)) {
				generic_handle_domain_irq(pc->domain, b->start + j);
				handled = true;
			}
		}
	}

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static int qsd8250_irq_domain_map(struct irq_domain *d, unsigned int irq,
				   irq_hw_number_t hwirq)
{
	struct msm_pinctrl *pc = d->host_data;

	irq_set_chip_data(irq, pc);
	irq_set_chip_and_handler(irq, &qsd8250_irq_chip, handle_edge_irq);
	irq_set_noprobe(irq);
	return 0;
}

static const struct irq_domain_ops qsd8250_irq_domain_ops = {
	.map = qsd8250_irq_domain_map,
	.xlate = irq_domain_xlate_twocell,
};

/* ---------------------------------------------------------------------- */
/* pinctrl_ops                                                            */
/* ---------------------------------------------------------------------- */

static int qsd8250_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct msm_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	return pc->gc.ngpio;
}

static const char *qsd8250_get_group_name(struct pinctrl_dev *pctldev,
					   unsigned int selector)
{
	struct msm_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	return pc->group_names[selector];
}

static int qsd8250_get_group_pins(struct pinctrl_dev *pctldev, unsigned int selector,
				   const unsigned int **pins, unsigned int *num_pins)
{
	struct msm_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	*pins = &pc->pins[selector].number;
	*num_pins = 1;
	return 0;
}

static const struct pinctrl_ops qsd8250_pctrl_ops = {
	.get_groups_count	= qsd8250_get_groups_count,
	.get_group_name		= qsd8250_get_group_name,
	.get_group_pins		= qsd8250_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_group,
	.dt_free_map		= pinconf_generic_dt_free_map,
};

/* ---------------------------------------------------------------------- */
/* pinmux_ops                                                             */
/* ---------------------------------------------------------------------- */

static int qsd8250_pmx_get_functions_count(struct pinctrl_dev *pctldev)
{
	return QSD8250_NFUNCS;
}

static const char *qsd8250_pmx_get_function_name(struct pinctrl_dev *pctldev,
						  unsigned int selector)
{
	struct msm_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	return pc->func_names[selector];
}

static int qsd8250_pmx_get_function_groups(struct pinctrl_dev *pctldev,
					    unsigned int selector,
					    const char * const **groups,
					    unsigned int *num_groups)
{
	struct msm_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	*groups = pc->group_names;
	*num_groups = pc->gc.ngpio;
	return 0;
}

static int qsd8250_pmx_set_mux(struct pinctrl_dev *pctldev,
				unsigned int func_selector,
				unsigned int group_selector)
{
	struct msm_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	unsigned int gpio = group_selector; /* 1 group == 1 pin */
	int dir;

	pc->func[gpio] = func_selector; /* selector N == raw func N */

	dir = qsd8250_gpio_get_direction(&pc->gc, gpio);
	if (dir < 0)
		dir = PCOM_GPIO_CFG_INPUT;

	return qsd8250_apply_pin_config(pc, gpio, dir);
}

static int qsd8250_pmx_gpio_request_enable(struct pinctrl_dev *pctldev,
					    struct pinctrl_gpio_range *range,
					    unsigned int offset)
{
	struct msm_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	pc->func[offset] = 0;
	return 0;
}

static const struct pinmux_ops qsd8250_pmx_ops = {
	.get_functions_count	= qsd8250_pmx_get_functions_count,
	.get_function_name	= qsd8250_pmx_get_function_name,
	.get_function_groups	= qsd8250_pmx_get_function_groups,
	.set_mux		= qsd8250_pmx_set_mux,
	.gpio_request_enable	= qsd8250_pmx_gpio_request_enable,
	.strict			= true,
};

/* ---------------------------------------------------------------------- */
/* pinconf_ops                                                            */
/* ---------------------------------------------------------------------- */

static int qsd8250_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
				unsigned long *config)
{
	struct msm_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	u32 arg;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		arg = pc->pull[pin] == PCOM_GPIO_CFG_NO_PULL;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		arg = pc->pull[pin] == PCOM_GPIO_CFG_PULL_DOWN;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		arg = pc->pull[pin] == PCOM_GPIO_CFG_PULL_UP;
		break;
	case PIN_CONFIG_BIAS_BUS_HOLD:
		arg = pc->pull[pin] == PCOM_GPIO_CFG_KEEPER;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		arg = (pc->drvstr[pin] + 1) * 2;
		break;
	default:
		return -ENOTSUPP;
	}

	if (param != PIN_CONFIG_DRIVE_STRENGTH && !arg)
		return -EINVAL;

	*config = pinconf_to_config_packed(param, arg);
	return 0;
}

static int qsd8250_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
				unsigned long *configs, unsigned int num_configs)
{
	struct msm_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	unsigned int i;
	int dir;

	for (i = 0; i < num_configs; i++) {
		enum pin_config_param param = pinconf_to_config_param(configs[i]);
		u32 arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			pc->pull[pin] = PCOM_GPIO_CFG_NO_PULL;
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			pc->pull[pin] = arg ? PCOM_GPIO_CFG_PULL_DOWN
					    : PCOM_GPIO_CFG_NO_PULL;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			pc->pull[pin] = arg ? PCOM_GPIO_CFG_PULL_UP
					    : PCOM_GPIO_CFG_NO_PULL;
			break;
		case PIN_CONFIG_BIAS_BUS_HOLD:
			pc->pull[pin] = PCOM_GPIO_CFG_KEEPER;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			if (arg < 2)
				arg = 2;
			if (arg > 16)
				arg = 16;
			pc->drvstr[pin] = (arg / 2) - 1;
			break;
		default:
			return -ENOTSUPP;
		}
	}

	dir = qsd8250_gpio_get_direction(&pc->gc, pin);
	if (dir < 0)
		dir = PCOM_GPIO_CFG_INPUT;

	return qsd8250_apply_pin_config(pc, pin, dir);
}

static int qsd8250_pinconf_group_set(struct pinctrl_dev *pctldev, unsigned int group,
				      unsigned long *configs, unsigned int num_configs)
{
	return qsd8250_pinconf_set(pctldev, group, configs, num_configs);
}

static const struct pinconf_ops qsd8250_pinconf_ops = {
	.pin_config_get		= qsd8250_pinconf_get,
	.pin_config_set		= qsd8250_pinconf_set,
	.pin_config_group_set	= qsd8250_pinconf_group_set,
};

static const struct pinctrl_desc qsd8250_pinctrl_desc_template = {
	.name		= "qsd8250-pinctrl",
	.pctlops	= &qsd8250_pctrl_ops,
	.pmxops		= &qsd8250_pmx_ops,
	.confops	= &qsd8250_pinconf_ops,
	.owner		= THIS_MODULE,
};

/* ---------------------------------------------------------------------- */
/* probe                                                                  */
/* ---------------------------------------------------------------------- */

static int qsd8250_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct msm_pinctrl *pc;
	unsigned int i;
	int ret;

	pc = devm_kzalloc(dev, sizeof(*pc), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;

	if (msm_gpio_banks[MSM_GPIO_NBANKS - 1].end + 1 != MSM_GPIO_NGPIO)
		dev_warn(dev,
			 "MSM_GPIO_NGPIO (%u) out of sync with bank table (%u) - fix the #define\n",
			 MSM_GPIO_NGPIO, msm_gpio_banks[MSM_GPIO_NBANKS - 1].end + 1);

	pc->dev = dev;
	raw_spin_lock_init(&pc->lock);

	for (i = 0; i < REGION_MAX; i++) {
		pc->regions[i] = devm_platform_ioremap_resource_byname(pdev,
							msm_gpio_region_names[i]);
		if (IS_ERR(pc->regions[i]))
			return dev_err_probe(dev, PTR_ERR(pc->regions[i]),
					      "failed to map %s region\n",
					      msm_gpio_region_names[i]);
	}

	for (i = 0; i < MSM_GPIO_NBANKS; i++) {
		const struct msm_gpio_bank_desc *b = &msm_gpio_banks[i];

		writel(0, bank_reg(pc, b, b->io_region, b->int_en_off));
		writel(~0U, bank_reg(pc, b, b->io_region, b->int_clear_off));
	}

	for (i = 0; i < MSM_GPIO_NGPIO; i++) {
		pc->pin_names[i] = devm_kasprintf(dev, GFP_KERNEL, "gpio%u", i);
		if (!pc->pin_names[i])
			return -ENOMEM;

		pc->pins[i].number = i;
		pc->pins[i].name = pc->pin_names[i];
		pc->group_names[i] = pc->pin_names[i];

		pc->func[i] = 0;
		pc->pull[i] = PCOM_GPIO_CFG_NO_PULL;
		pc->drvstr[i] = PCOM_GPIO_CFG_8MA;
	}

	pc->func_names[0] = "gpio";
	for (i = 1; i < QSD8250_NFUNCS; i++) {
		pc->func_names[i] = devm_kasprintf(dev, GFP_KERNEL, "func%u", i);
		if (!pc->func_names[i])
			return -ENOMEM;
	}

	pc->desc = qsd8250_pinctrl_desc_template;
	pc->desc.pins = pc->pins;
	pc->desc.npins = MSM_GPIO_NGPIO;

	pc->gc.label = "qsd8250-pinctrl";
	pc->gc.parent = dev;
	pc->gc.owner = THIS_MODULE;
	pc->gc.request = gpiochip_generic_request;
	pc->gc.free = gpiochip_generic_free;
	pc->gc.set_config = gpiochip_generic_config;
	pc->gc.get_direction = qsd8250_gpio_get_direction;
	pc->gc.direction_input = qsd8250_gpio_direction_input;
	pc->gc.direction_output = qsd8250_gpio_direction_output;
	pc->gc.get = qsd8250_gpio_get;
	pc->gc.set = qsd8250_gpio_set;
	pc->gc.to_irq = qsd8250_gpio_to_irq;
	pc->gc.base = -1;
	pc->gc.ngpio = MSM_GPIO_NGPIO;
	pc->gc.fwnode = dev_fwnode(dev);
	pc->gc.can_sleep = true;

	pc->domain = irq_domain_create_linear(dev_fwnode(dev), MSM_GPIO_NGPIO,
					       &qsd8250_irq_domain_ops, pc);
	if (!pc->domain)
		return dev_err_probe(dev, -ENOMEM, "failed to add irq domain\n");

	ret = devm_gpiochip_add_data(dev, &pc->gc, pc);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register gpiochip\n");

	pc->pctldev = devm_pinctrl_register(dev, &pc->desc, pc);
	if (IS_ERR(pc->pctldev))
		return dev_err_probe(dev, PTR_ERR(pc->pctldev),
				      "failed to register pinctrl\n");

	ret = gpiochip_add_pin_range(&pc->gc, dev_name(dev), 0, 0, MSM_GPIO_NGPIO);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add gpio-pin range\n");

	pc->n_parent_irqs = platform_irq_count(pdev);
	if (pc->n_parent_irqs <= 0 || pc->n_parent_irqs > ARRAY_SIZE(pc->parent_irqs))
		return dev_err_probe(dev, -EINVAL,
				      "expected 1-2 parent interrupts, got %d\n",
				      pc->n_parent_irqs);

	for (i = 0; i < pc->n_parent_irqs; i++) {
		pc->parent_irqs[i] = platform_get_irq(pdev, i);
		if (pc->parent_irqs[i] < 0)
			return pc->parent_irqs[i];

		ret = devm_request_irq(dev, pc->parent_irqs[i], qsd8250_irq_handler,
					IRQF_NO_SUSPEND, dev_name(dev), pc);
		if (ret)
			return dev_err_probe(dev, ret,
					      "failed to request irq %d\n",
					      pc->parent_irqs[i]);
	}

	platform_set_drvdata(pdev, pc);
	dev_info(dev, "qsd8250 TLMM pinctrl/GPIO controller, %u lines, %d parent irqs\n",
		 MSM_GPIO_NGPIO, pc->n_parent_irqs);
	return 0;
}

static const struct of_device_id qsd8250_pinctrl_of_match[] = {
	{ .compatible = "qcom,qsd8250-pinctrl" },
	{ }
};
MODULE_DEVICE_TABLE(of, qsd8250_pinctrl_of_match);

static struct platform_driver qsd8250_pinctrl_driver = {
	.probe = qsd8250_pinctrl_probe,
	.driver = {
		.name = "pinctrl-qsd8250",
		.of_match_table = qsd8250_pinctrl_of_match,
	},
};
module_platform_driver(qsd8250_pinctrl_driver);

MODULE_DESCRIPTION("Qualcomm QSD8250 TLMM pinctrl/GPIO driver");
MODULE_LICENSE("GPL v2");