// SPDX-License-Identifier: GPL-2.0-only
/*
 * Jogball RGB backlight driver for the HTC MicroP companion chip
 * (HTC Passion / Nexus One).
 *
 * This is its own i2c_client on htc-microp's virtual bus (its own DT node,
 * its own pseudo "reg" address) - separate from the top LED and keypad
 * backlight, which are their own sibling nodes/drivers.
 *
 * Ported from the downstream board-mahimahi-microp.c LED code:
 *   Copyright (C) 2009 Google, Inc
 *   Copyright (C) 2009 HTC Corporation.
 *
 * Copyright (C) 2024
 */

#include <linux/i2c.h>
#include <linux/led-class-multicolor.h>
#include <linux/leds.h>
#include <linux/microp.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>

struct microp_jogball_priv {
	struct i2c_client	*client;
	struct led_classdev_mc	mc;
	struct mc_subled	subled[3];
};

/*
 * Color and on/off are two separate commands. PWM_SET (0x5C) sets the
 * R/G/B mix; MODE (0x5A) turns the whole thing on or off. Downstream's
 * MODE payload for "on" ({1, 0xff, 0xff}) and "off" ({0, 0, 0}) is what's
 * replicated here; downstream also has a third "blink" mode
 * ({2, 0, 60}) which isn't wired up yet - the kernel LED multicolor
 * framework doesn't have a natural slot for a hardware-native blink
 * pattern like this, so for now brightness=0/on is all this driver
 * does. Worth revisiting with a trigger or a custom sysfs attribute if
 * you want the blink mode back.
 */
static int microp_jogball_write_mode(struct i2c_client *client, bool on)
{
	u8 data[3];

	if (on) {
		data[0] = 1;
		data[1] = 0xff;
		data[2] = 0xff;
	} else {
		data[0] = 0;
		data[1] = 0;
		data[2] = 0;
	}

	return i2c_smbus_write_i2c_block_data(client, MICROP_I2C_WCMD_JOGBALL_LED_MODE,
					       sizeof(data), data);
}

static int microp_jogball_write_color(struct i2c_client *client, u8 r, u8 g, u8 b)
{
	u8 data[4] = { r, g, b, 0x00 };

	return i2c_smbus_write_i2c_block_data(client, MICROP_I2C_WCMD_JOGBALL_LED_PWM_SET,
					       sizeof(data), data);
}

static int microp_jogball_brightness_set(struct led_classdev *cdev, enum led_brightness brightness)
{
	struct led_classdev_mc *mc = lcdev_to_mccdev(cdev);
	struct microp_jogball_priv *priv = container_of(mc, struct microp_jogball_priv, mc);
	int ret;

	led_mc_calc_color_components(mc, brightness);

	ret = microp_jogball_write_color(priv->client,
					  mc->subled_info[0].brightness,
					  mc->subled_info[1].brightness,
					  mc->subled_info[2].brightness);
	if (ret)
		return ret;

	return microp_jogball_write_mode(priv->client, brightness != 0);
}

static int microp_jogball_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct microp_jogball_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;

	priv->subled[0].color_index = LED_COLOR_ID_RED;
	priv->subled[1].color_index = LED_COLOR_ID_GREEN;
	priv->subled[2].color_index = LED_COLOR_ID_BLUE;

	priv->mc.led_cdev.name = "microp:jogball";
	priv->mc.led_cdev.max_brightness = 255;
	priv->mc.led_cdev.brightness_set_blocking = microp_jogball_brightness_set;
	priv->mc.num_colors = ARRAY_SIZE(priv->subled);
	priv->mc.subled_info = priv->subled;

	ret = devm_led_classdev_multicolor_register(dev, &priv->mc);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register jogball led\n");

	return 0;
}

static const struct of_device_id microp_jogball_of_match[] = {
	{ .compatible = "htc,microp-jogball-led" },
	{ }
};
MODULE_DEVICE_TABLE(of, microp_jogball_of_match);

static const struct i2c_device_id microp_jogball_i2c_id[] = {
	{ "htc-microp-jogball" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, microp_jogball_i2c_id);

static struct i2c_driver microp_jogball_driver = {
	.driver = {
		.name		= "htc-microp-jogball-led",
		.of_match_table	= microp_jogball_of_match,
	},
	.probe		= microp_jogball_probe,
	.id_table	= microp_jogball_i2c_id,
};
module_i2c_driver(microp_jogball_driver);

MODULE_DESCRIPTION("HTC MicroP jogball RGB backlight driver");
MODULE_LICENSE("GPL v2");