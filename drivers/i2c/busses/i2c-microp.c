// SPDX-License-Identifier: GPL-2.0-only
/*
 * HTC MicroP gpio extender chip driver.
 *
 *
 * Ported from the LK/UEFI htcleo microp driver:
 *   Copyright (c) 2012, Shantanu Gupta <shans95g@gmail.com>
 *   Based on the open source driver from HTC.
 *
 * Copyright (C) 2026 J0SH1X <aljoshua.hell@gmail.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/microp.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>

#define MICROP_MAX_WRITE_LEN	21

struct microp_data {
	struct i2c_client	*client;
	struct gpio_desc	*gpio_reset;
	struct i2c_adapter	virt_adap;
};

static int microp_virt_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct microp_data *microp = i2c_get_adapdata(adap);
	u16 saved_addr[8];
	int i, ret;

	if (num > ARRAY_SIZE(saved_addr))
		return -EOPNOTSUPP;

	for (i = 0; i < num; i++) {
		saved_addr[i] = msgs[i].addr;
		msgs[i].addr = microp->client->addr;
	}

	ret = i2c_transfer(microp->client->adapter, msgs, num);

	for (i = 0; i < num; i++)
		msgs[i].addr = saved_addr[i];

	return ret;
}

static u32 microp_virt_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C;
}

static const struct i2c_algorithm microp_virt_algo = {
	.master_xfer	= microp_virt_xfer,
	.functionality	= microp_virt_functionality,
};

static const struct i2c_adapter_quirks microp_virt_quirks = {
	.max_write_len	= MICROP_MAX_WRITE_LEN,
};

static int microp_read_version(struct microp_data *microp, u8 *version, size_t len)
{
	u8 cmd = MICROP_I2C_RCMD_VERSION;
	struct i2c_msg msgs[2] = {
		{ .addr = microp->client->addr, .flags = 0,        .len = 1,   .buf = &cmd },
		{ .addr = microp->client->addr, .flags = I2C_M_RD, .len = len, .buf = version },
	};

	return i2c_transfer(microp->client->adapter, msgs, 2) == 2 ? 0 : -EIO;
}

static int microp_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct microp_data *microp;
	u8 version[2];
	int ret;

	microp = devm_kzalloc(dev, sizeof(*microp), GFP_KERNEL);
	if (!microp)
		return -ENOMEM;

	microp->client = client;
	i2c_set_clientdata(client, microp);

	microp->gpio_reset = devm_gpiod_get_optional(dev, "gpio-reset", GPIOD_OUT_LOW);
	if (IS_ERR(microp->gpio_reset))
		return dev_err_probe(dev, PTR_ERR(microp->gpio_reset), "failed to get reset gpio\n");

	if (microp->gpio_reset) {
		gpiod_set_value_cansleep(microp->gpio_reset, 1);
		usleep_range(1000, 2000);
	}

	ret = microp_read_version(microp, version, sizeof(version));
	if (ret)
		return dev_err_probe(dev, ret, "failed to read chip version, is it present?\n");

	dev_info(dev, "HTC MicroP version 0x%02x 0x%02x\n", version[0], version[1]);

	microp->virt_adap.owner = THIS_MODULE;
	microp->virt_adap.algo = &microp_virt_algo;
	microp->virt_adap.quirks = &microp_virt_quirks;
	microp->virt_adap.dev.parent = dev;
	microp->virt_adap.dev.of_node = dev->of_node;
	strscpy(microp->virt_adap.name, "htc-microp", sizeof(microp->virt_adap.name));
	i2c_set_adapdata(&microp->virt_adap, microp);

	return devm_i2c_add_adapter(dev, &microp->virt_adap);
}

static const struct of_device_id microp_of_match[] = {
	{ .compatible = "htc,qsd8250-microp" },
	{ }
};
MODULE_DEVICE_TABLE(of, microp_of_match);

static const struct i2c_device_id microp_i2c_id[] = {
	{ "htc-qsd8250-microp" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, microp_i2c_id);

static struct i2c_driver microp_driver = {
	.driver = {
		.name		= "htc-qsd8250-microp",
		.of_match_table	= microp_of_match,
	},
	.probe		= microp_probe,
	.id_table	= microp_i2c_id,
};
module_i2c_driver(microp_driver);

MODULE_AUTHOR("J0SH1X <aljoshua.hell@gmail.com>");
MODULE_DESCRIPTION("HTC MicroP companion chip driver");
MODULE_LICENSE("GPL v2");