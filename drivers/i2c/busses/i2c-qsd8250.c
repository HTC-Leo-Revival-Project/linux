// SPDX-License-Identifier: GPL-2.0-only
/*
 * I2C bus driver for the Qualcomm qsd8250 I2C controller
 *
 * Ported from the LK/UEFI msm-i2c driver:
 *   Copyright (C) 2007 Google, Inc.
 *   Copyright (C) 2011 Alexander Tarasikov <alexander.tarasikov@gmail.com>
 * to the mainline Linux i2c subsystem.
 *
 * Copyright (C) 2026 J0SH1X <aljoshua.hell@gmail.com>
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>

extern int pcom_gpio_tlmm_config(unsigned int config, unsigned int disable);

#define MSM_GPIO_CFG(gpio, func, dir, pull, drvstr) (				\
	(((gpio) & 0x3FF) << 4)  |						\
	((func) & 0xf)           |						\
	(((dir) & 0x1) << 14)    |						\
	(((pull) & 0x3) << 15)   |						\
	(((drvstr) & 0xF) << 17)						\
)

enum {
	MSM_GPIO_CFG_INPUT,
	MSM_GPIO_CFG_OUTPUT,
};

enum {
	MSM_GPIO_CFG_NO_PULL,
	MSM_GPIO_CFG_PULL_DOWN,
	MSM_GPIO_CFG_KEEPER,
	MSM_GPIO_CFG_PULL_UP,
};

enum {
	MSM_GPIO_CFG_2MA,
	MSM_GPIO_CFG_4MA,
	MSM_GPIO_CFG_6MA,
	MSM_GPIO_CFG_8MA,
	MSM_GPIO_CFG_10MA,
	MSM_GPIO_CFG_12MA,
	MSM_GPIO_CFG_14MA,
	MSM_GPIO_CFG_16MA,
};

#define I2C_WRITE_DATA			0x00
#define I2C_CLK_CTL			0x04
#define I2C_STATUS			0x08
#define I2C_READ_DATA			0x0c
#define I2C_INTERFACE_SELECT		0x10

#define I2C_WRITE_DATA_DATA_BYTE	0xff
#define I2C_WRITE_DATA_ADDR_BYTE	BIT(8)
#define I2C_WRITE_DATA_LAST_BYTE	BIT(9)

#define I2C_CLK_CTL_FS_DIVIDER_MASK	0xff
#define I2C_CLK_CTL_HS_DIVIDER_SHIFT	8
#define I2C_CLK_CTL_HS_DIVIDER_MASK	(0x7 << I2C_CLK_CTL_HS_DIVIDER_SHIFT)

#define I2C_STATUS_WR_BUFFER_FULL	BIT(0)
#define I2C_STATUS_RD_BUFFER_FULL	BIT(1)
#define I2C_STATUS_BUS_ERROR		BIT(2)
#define I2C_STATUS_PACKET_NACKED	BIT(3)
#define I2C_STATUS_ARB_LOST		BIT(4)
#define I2C_STATUS_INVALID_WRITE	BIT(5)
#define I2C_STATUS_FAILED		(3 << 6)
#define I2C_STATUS_BUS_ACTIVE		BIT(8)
#define I2C_STATUS_BUS_MASTER		BIT(9)
#define I2C_STATUS_ERROR_MASK		0xfc

#define I2C_INTERFACE_SELECT_SCL	BIT(8)
#define I2C_INTERFACE_SELECT_SDA	BIT(9)

#define MSM_I2C_DEFAULT_CLK_HZ		100000
#define MSM_I2C_SRC_CLK_HZ		19200000
#define MSM_I2C_POLL_NOTBUSY_TRIES	200
#define MSM_I2C_XFER_TIMEOUT_MS		1000
#define MSM_I2C_BUS_RECOVERY_CLOCKS	9

struct msm_i2c_dev {
	struct device		*dev;
	void __iomem		*base;
	int			irq;
	struct clk		*clk;
	struct i2c_adapter	adap;
	struct gpio_desc	*gpio_scl;
	struct gpio_desc	*gpio_sda;
	unsigned int		pin_scl;
	unsigned int		pin_sda;

	struct i2c_msg		*msg;
	int			rem;
	int			pos;
	int			cnt;
	bool			need_flush;
	int			flush_cnt;
	int			xfer_result;

	struct completion	complete;
};

static void msm_i2c_write_delay(struct msm_i2c_dev *dev)
{
	if (readl(dev->base + I2C_INTERFACE_SELECT) & I2C_INTERFACE_SELECT_SCL)
		return;

	ndelay(6000);
}

static bool msm_i2c_fill_write_buffer(struct msm_i2c_dev *dev)
{
	u16 val;

	if (dev->pos < 0) {
		val = I2C_WRITE_DATA_ADDR_BYTE | (i2c_8bit_addr_from_msg(dev->msg));

		if (dev->rem == 1 && dev->msg->len == 0)
			val |= I2C_WRITE_DATA_LAST_BYTE;

		msm_i2c_write_delay(dev);
		writel(val, dev->base + I2C_WRITE_DATA);
		dev->pos++;
		return true;
	}

	if (dev->msg->flags & I2C_M_RD)
		return false;

	if (!dev->cnt)
		return false;

	val = dev->msg->buf[dev->pos];
	if (dev->cnt == 1 && dev->rem == 1)
		val |= I2C_WRITE_DATA_LAST_BYTE;

	msm_i2c_write_delay(dev);
	writel(val, dev->base + I2C_WRITE_DATA);
	dev->pos++;
	dev->cnt--;
	return true;
}

static void msm_i2c_read_buffer(struct msm_i2c_dev *dev)
{
	if ((dev->msg->flags & I2C_M_RD) && dev->pos >= 0 && dev->cnt) {
		switch (dev->cnt) {
		case 1:
			if (dev->pos != 0)
				break;
			dev->need_flush = true;
			fallthrough;
		case 2:
			writel(I2C_WRITE_DATA_LAST_BYTE, dev->base + I2C_WRITE_DATA);
		}
		dev->msg->buf[dev->pos] = readl(dev->base + I2C_READ_DATA);
		dev->cnt--;
		dev->pos++;
	} else {
		if (dev->flush_cnt & 1) {
			writel(I2C_WRITE_DATA_LAST_BYTE, dev->base + I2C_WRITE_DATA);
		}

		readl(dev->base + I2C_READ_DATA);

		if (dev->need_flush)
			dev->need_flush = false;
		else
			dev->flush_cnt++;
	}
}

static irqreturn_t msm_i2c_interrupt(int irq, void *devid)
{
	struct msm_i2c_dev *dev = devid;
	u32 status = readl(dev->base + I2C_STATUS);
	bool not_done = true;

	if (!dev->msg) {
		dev_dbg(dev->dev, "IRQ but nothing to do, status 0x%x\n", status);
		return IRQ_HANDLED;
	}

	if (status & I2C_STATUS_ERROR_MASK)
		goto err;

	if (!(status & I2C_STATUS_WR_BUFFER_FULL))
		not_done = msm_i2c_fill_write_buffer(dev);

	if (status & I2C_STATUS_RD_BUFFER_FULL)
		msm_i2c_read_buffer(dev);

	if (dev->pos >= 0 && dev->cnt == 0) {
		if (dev->rem > 1) {
			dev->rem--;
			dev->msg++;
			dev->pos = -1;
			dev->cnt = dev->msg->len;
		} else if (!not_done && !dev->need_flush) {
			dev->xfer_result = 0;
			complete(&dev->complete);
		}
	}

	return IRQ_HANDLED;

err:
	dev_err(dev->dev, "bus error, status 0x%x\n", status);
	dev->xfer_result = -EIO;
	complete(&dev->complete);
	return IRQ_HANDLED;
}

static void msm_i2c_set_gpio_mux(struct msm_i2c_dev *dev, int mux_to_i2c)
{
	int ret_scl, ret_sda;

	if (mux_to_i2c) {
		ret_scl = pcom_gpio_tlmm_config(MSM_GPIO_CFG(dev->pin_scl, 0, MSM_GPIO_CFG_OUTPUT,
							       MSM_GPIO_CFG_NO_PULL, MSM_GPIO_CFG_8MA), 0);
		ret_sda = pcom_gpio_tlmm_config(MSM_GPIO_CFG(dev->pin_sda, 0, MSM_GPIO_CFG_OUTPUT,
							       MSM_GPIO_CFG_NO_PULL, MSM_GPIO_CFG_8MA), 0);
	} else {
		ret_scl = pcom_gpio_tlmm_config(MSM_GPIO_CFG(dev->pin_scl, 1, MSM_GPIO_CFG_INPUT,
							       MSM_GPIO_CFG_NO_PULL, MSM_GPIO_CFG_2MA), 0);
		ret_sda = pcom_gpio_tlmm_config(MSM_GPIO_CFG(dev->pin_sda, 1, MSM_GPIO_CFG_INPUT,
							       MSM_GPIO_CFG_NO_PULL, MSM_GPIO_CFG_2MA), 0);
	}

	if (ret_scl || ret_sda)
		dev_warn(dev->dev,
			 "pcom_gpio_tlmm_config failed: mux_to_i2c=%d pin_scl=%u ret=%d, pin_sda=%u ret=%d\n",
			 mux_to_i2c, dev->pin_scl, ret_scl, dev->pin_sda, ret_sda);
}

static int msm_i2c_poll_notbusy(struct msm_i2c_dev *dev, bool warn)
{
	unsigned int retries = 0;

	while (retries != MSM_I2C_POLL_NOTBUSY_TRIES) {
		u32 status = readl(dev->base + I2C_STATUS);

		if (!(status & I2C_STATUS_BUS_ACTIVE)) {
			if (retries && warn)
				dev_dbg(dev->dev, "bus was busy (%u retries)\n", retries);
			return 0;
		}

		if (retries++ > 100)
			udelay(10);
	}

	dev_err(dev->dev, "timed out waiting for bus not-busy\n");
	return -ETIMEDOUT;
}

static int msm_i2c_recover_bus_busy(struct msm_i2c_dev *dev)
{
	int i;
	bool scl_high = false;
	u32 status = readl(dev->base + I2C_STATUS);

	if (!(status & (I2C_STATUS_BUS_ACTIVE | I2C_STATUS_WR_BUFFER_FULL)))
		return 0;

	msm_i2c_set_gpio_mux(dev, 0);

	if (status & I2C_STATUS_RD_BUFFER_FULL) {
		writel(I2C_WRITE_DATA_LAST_BYTE, dev->base + I2C_WRITE_DATA);
		readl(dev->base + I2C_READ_DATA);
	} else if (status & I2C_STATUS_BUS_MASTER) {
		writel(I2C_WRITE_DATA_LAST_BYTE | 0xff, dev->base + I2C_WRITE_DATA);
	}

	gpiod_direction_output(dev->gpio_scl, 0);
	gpiod_direction_output(dev->gpio_sda, 0);

	for (i = 0; i < MSM_I2C_BUS_RECOVERY_CLOCKS; i++) {
		if (gpiod_get_value_cansleep(dev->gpio_sda) && scl_high)
			break;

		gpiod_set_value_cansleep(dev->gpio_scl, 0);
		ndelay(5000);
		gpiod_set_value_cansleep(dev->gpio_sda, 0);
		ndelay(5000);

		gpiod_direction_input(dev->gpio_scl);
		ndelay(5000);

		if (!gpiod_get_value_cansleep(dev->gpio_scl))
			udelay(20);
		if (!gpiod_get_value_cansleep(dev->gpio_scl))
			udelay(10);

		scl_high = gpiod_get_value_cansleep(dev->gpio_scl);
		gpiod_direction_input(dev->gpio_sda);
		ndelay(5000);
	}

	gpiod_direction_input(dev->gpio_scl);
	gpiod_direction_input(dev->gpio_sda);

	msm_i2c_set_gpio_mux(dev, 1);

	ndelay(10000);

	status = readl(dev->base + I2C_STATUS);
	if (!(status & I2C_STATUS_BUS_ACTIVE)) {
		dev_dbg(dev->dev, "bus recovered after %d clocks\n", i);
		return 0;
	}

	dev_err(dev->dev, "bus still busy after recovery, status 0x%x\n", status);
	return -EAGAIN;
}

static int msm_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct msm_i2c_dev *dev = i2c_get_adapdata(adap);
	int ret;
	unsigned long time_left;

	ret = clk_prepare_enable(dev->clk);
	if (ret)
		return ret;

	ret = msm_i2c_poll_notbusy(dev, true);
	if (ret) {
		ret = msm_i2c_recover_bus_busy(dev);
		if (ret)
			goto out;
	}

	if (dev->flush_cnt)
		dev_dbg(dev->dev, "%d unrequested bytes read last time\n", dev->flush_cnt);

	reinit_completion(&dev->complete);
	dev->msg = msgs;
	dev->rem = num;
	dev->pos = -1;
	dev->cnt = msgs->len;
	dev->need_flush = false;
	dev->flush_cnt = 0;
	dev->xfer_result = -ETIMEDOUT;

	msm_i2c_interrupt(dev->irq, dev);

	time_left = wait_for_completion_timeout(&dev->complete,
			msecs_to_jiffies(MSM_I2C_XFER_TIMEOUT_MS));

	ret = dev->xfer_result;
	dev->msg = NULL;
	dev->rem = 0;
	dev->pos = 0;
	dev->cnt = 0;

	if (!time_left) {
		dev_err(dev->dev, "transfer timed out\n");
		ret = -ETIMEDOUT;
	}

	if (msm_i2c_poll_notbusy(dev, false))
		msm_i2c_recover_bus_busy(dev);

	if (ret == 0)
		ret = num;

out:
	clk_disable_unprepare(dev->clk);
	return ret;
}

static u32 msm_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm msm_i2c_algorithm = {
	.master_xfer	= msm_i2c_xfer,
	.functionality	= msm_i2c_functionality,
};

static void msm_i2c_init_clk_ctl(struct msm_i2c_dev *dev, u32 clock_freq)
{
	unsigned int fs_div, hs_div = 3;
	u32 clk_ctl;

	if (clock_freq < 100000 || clock_freq > 400000)
		clock_freq = MSM_I2C_DEFAULT_CLK_HZ;

	fs_div = ((MSM_I2C_SRC_CLK_HZ / clock_freq) / 2) - 3;
	clk_ctl = ((hs_div & 0x7) << I2C_CLK_CTL_HS_DIVIDER_SHIFT) |
		  (fs_div & I2C_CLK_CTL_FS_DIVIDER_MASK);

	writel(clk_ctl, dev->base + I2C_CLK_CTL);
	dev_dbg(dev->dev, "clk_ctl 0x%x -> %u Hz\n", clk_ctl,
		MSM_I2C_SRC_CLK_HZ / (2 * ((clk_ctl & 0xff) + 3)));
}

static int msm_i2c_probe(struct platform_device *pdev)
{
	struct device *devnode = &pdev->dev;
	struct msm_i2c_dev *dev;
	u32 clock_freq;
	int ret;

	dev = devm_kzalloc(devnode, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->dev = devnode;
	dev->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dev->base))
		return PTR_ERR(dev->base);

	dev->irq = platform_get_irq(pdev, 0);
	if (dev->irq < 0)
		return dev->irq;

	dev->clk = devm_clk_get(devnode, "core");
	if (IS_ERR(dev->clk))
		return dev_err_probe(devnode, PTR_ERR(dev->clk), "failed to get clock\n");

	dev->gpio_scl = devm_gpiod_get(devnode, "scl", GPIOD_IN);
	if (IS_ERR(dev->gpio_scl))
		return dev_err_probe(devnode, PTR_ERR(dev->gpio_scl), "failed to get scl-gpios\n");

	dev->gpio_sda = devm_gpiod_get(devnode, "sda", GPIOD_IN);
	if (IS_ERR(dev->gpio_sda))
		return dev_err_probe(devnode, PTR_ERR(dev->gpio_sda), "failed to get sda-gpios\n");

	if (of_property_read_u32(devnode->of_node, "qcom,scl-pin", &dev->pin_scl))
		return dev_err_probe(devnode, -EINVAL, "missing qcom,scl-pin\n");
	if (of_property_read_u32(devnode->of_node, "qcom,sda-pin", &dev->pin_sda))
		return dev_err_probe(devnode, -EINVAL, "missing qcom,sda-pin\n");

	init_completion(&dev->complete);

	if (of_property_read_u32(devnode->of_node, "clock-frequency", &clock_freq))
		clock_freq = MSM_I2C_DEFAULT_CLK_HZ;

	msm_i2c_set_gpio_mux(dev, 0);

	ret = clk_prepare_enable(dev->clk);
	if (ret)
		return ret;
	msm_i2c_init_clk_ctl(dev, clock_freq);
	clk_disable_unprepare(dev->clk);

	ret = devm_request_irq(devnode, dev->irq, msm_i2c_interrupt,
				IRQF_TRIGGER_RISING, dev_name(devnode), dev);
	if (ret)
		return dev_err_probe(devnode, ret, "failed to request irq\n");

	dev->adap.owner = THIS_MODULE;
	dev->adap.algo = &msm_i2c_algorithm;
	dev->adap.dev.parent = devnode;
	dev->adap.dev.of_node = devnode->of_node;
	strscpy(dev->adap.name, "MSM7xxx I2C adapter", sizeof(dev->adap.name));
	i2c_set_adapdata(&dev->adap, dev);

	platform_set_drvdata(pdev, dev);

	return i2c_add_adapter(&dev->adap);
}

static void msm_i2c_remove(struct platform_device *pdev)
{
	struct msm_i2c_dev *dev = platform_get_drvdata(pdev);

	i2c_del_adapter(&dev->adap);
}

static const struct of_device_id msm_i2c_dt_match[] = {
	{ .compatible = "qcom,qsd8250-i2c" },
	{ }
};
MODULE_DEVICE_TABLE(of, msm_i2c_dt_match);

static struct platform_driver msm_i2c_driver = {
	.probe	= msm_i2c_probe,
	.remove	= msm_i2c_remove,
	.driver	= {
		.name		= "i2c-qsd8250",
		.of_match_table	= msm_i2c_dt_match,
	},
};
module_platform_driver(msm_i2c_driver);

MODULE_AUTHOR("J0SH1X <aljoshua.hell@gmail.com>");
MODULE_DESCRIPTION("Qualcomm qsd8250 I2C bus driver");
MODULE_LICENSE("GPL v2");