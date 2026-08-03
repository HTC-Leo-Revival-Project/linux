// SPDX-License-Identifier: GPL-2.0-only
/*
 * Top notification LED (green/amber) driver for the HTC MicroP companion
 * chip (HTC Passion / Nexus One).
 *
 * This is its own i2c_client on htc-microp's virtual bus (its own DT node,
 * its own pseudo "reg" address) - separate from the jogball and keypad
 * backlight, which are their own sibling nodes/drivers.
 *
 * Ported from the downstream board-mahimahi-microp.c LED code:
 *   Copyright (C) 2009 Google, Inc
 *   Copyright (C) 2009 HTC Corporation.
 *
 * Copyright (C) 2024
 */

#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/microp.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>

struct microp_led_priv {
	struct i2c_client	*client;
	struct led_classdev	green;
	struct led_classdev	amber;
};

/*
 * MICROP_I2C_WCMD_LED_MODE (0x53), a 7-byte command shared by both
 * colors. data[0] selects which color this write applies to (0x01 =
 * green, 0x02 = amber), and the remaining 6 bytes are split into two
 * 3-byte {mode, off_timer_hi, off_timer_lo} sub-fields - green uses the
 * first, amber the second - so a green write leaves amber's bytes
 * zeroed and vice versa. off_timer=0xffff means "never auto switch
 * off", matching every plain brightness_set call from downstream.
 */
static int microp_led_write_mode(struct i2c_client *client, bool amber, u8 mode, u16 off_timer)
{
	u8 data[7] = { 0 };

	if (!amber) {
		data[0] = 0x01;
		data[1] = mode;
		data[2] = off_timer >> 8;
		data[3] = off_timer & 0xff;
	} else {
		data[0] = 0x02;
		data[4] = mode;
		data[5] = off_timer >> 8;
		data[6] = off_timer & 0xff;
	}

	return i2c_smbus_write_i2c_block_data(client, MICROP_I2C_WCMD_LED_MODE,
					       sizeof(data), data);
}

static int microp_led_green_set(struct led_classdev *cdev, enum led_brightness brightness)
{
	struct microp_led_priv *priv = container_of(cdev, struct microp_led_priv, green);

	return microp_led_write_mode(priv->client, false, brightness ? 1 : 0, 0xffff);
}

static int microp_led_amber_set(struct led_classdev *cdev, enum led_brightness brightness)
{
	struct microp_led_priv *priv = container_of(cdev, struct microp_led_priv, amber);

	return microp_led_write_mode(priv->client, true, brightness ? 1 : 0, 0xffff);
}

static int microp_led_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct microp_led_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;

	priv->green.name = "microp:green";
	priv->green.max_brightness = 1;
	priv->green.brightness_set_blocking = microp_led_green_set;
	ret = devm_led_classdev_register(dev, &priv->green);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register green led\n");

	priv->amber.name = "microp:amber";
	priv->amber.max_brightness = 1;
	priv->amber.brightness_set_blocking = microp_led_amber_set;
	ret = devm_led_classdev_register(dev, &priv->amber);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register amber led\n");

	return 0;
}

static const struct of_device_id microp_led_of_match[] = {
	{ .compatible = "htc,microp-led" },
	{ }
};
MODULE_DEVICE_TABLE(of, microp_led_of_match);

static const struct i2c_device_id microp_led_i2c_id[] = {
	{ "htc-microp-led" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, microp_led_i2c_id);

static struct i2c_driver microp_led_driver = {
	.driver = {
		.name		= "htc-microp-led",
		.of_match_table	= microp_led_of_match,
	},
	.probe		= microp_led_probe,
	.id_table	= microp_led_i2c_id,
};
module_i2c_driver(microp_led_driver);

MODULE_DESCRIPTION("HTC MicroP top LED (green/amber) driver");
MODULE_LICENSE("GPL v2");