// SPDX-License-Identifier: GPL-2.0-only
/*
 * Keypad backlight driver for the HTC MicroP companion chip
 * (HTC Passion / Nexus One).
 *
 * This is its own i2c_client on htc-microp's virtual bus (its own DT node,
 * its own pseudo "reg" address) - separate from the top LED and jogball
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

struct microp_kbd_backlight_priv {
	struct i2c_client	*client;
	struct led_classdev	cdev;
	u8			last_value;	/* avoid a flicker from re-writing
						 * the same value - same as
						 * downstream. */
};

/*
 * MICROP_I2C_WCMD_BUTTONS_LED_CTRL (0x25), a 4-byte command. data[0]=0x05
 * ("in 40ms", per downstream's comment), data[1] is the duty cycle but
 * downstream only ever writes a fixed 0x20 for "on" or 0 for "off"
 * rather than a real 0-255 sweep, and data[3]=0x04 (bit2) means "change
 * brightness". Kept identical here - this driver's max_brightness is
 * 255 so userspace can still write any value, but only the on/off
 * distinction reaches the hardware, matching exactly what downstream did.
 */
static int microp_kbd_backlight_set(struct led_classdev *cdev, enum led_brightness brightness)
{
	struct microp_kbd_backlight_priv *priv =
		container_of(cdev, struct microp_kbd_backlight_priv, cdev);
	u8 value = brightness >= 255 ? 0x20 : 0x00;
	u8 data[4] = { 0x05, value, 0x00, 0x04 };

	if (priv->last_value == value)
		return 0;

	priv->last_value = value;

	return i2c_smbus_write_i2c_block_data(priv->client, MICROP_I2C_WCMD_BUTTONS_LED_CTRL,
					       sizeof(data), data);
}

static int microp_kbd_backlight_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct microp_kbd_backlight_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	priv->last_value = 0xff; /* doesn't match 0x00/0x20, forces the first write through */

	priv->cdev.name = "microp:kbd-backlight";
	priv->cdev.max_brightness = 255;
	priv->cdev.brightness_set_blocking = microp_kbd_backlight_set;
	ret = devm_led_classdev_register(dev, &priv->cdev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register keypad backlight\n");

	return 0;
}

static const struct of_device_id microp_kbd_backlight_of_match[] = {
	{ .compatible = "htc,microp-kbd-backlight" },
	{ }
};
MODULE_DEVICE_TABLE(of, microp_kbd_backlight_of_match);

static const struct i2c_device_id microp_kbd_backlight_i2c_id[] = {
	{ "htc-microp-kbd-bl" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, microp_kbd_backlight_i2c_id);

static struct i2c_driver microp_kbd_backlight_driver = {
	.driver = {
		.name		= "htc-microp-kbd-backlight",
		.of_match_table	= microp_kbd_backlight_of_match,
	},
	.probe		= microp_kbd_backlight_probe,
	.id_table	= microp_kbd_backlight_i2c_id,
};
module_i2c_driver(microp_kbd_backlight_driver);

MODULE_DESCRIPTION("HTC MicroP keypad backlight driver");
MODULE_LICENSE("GPL v2");