// SPDX-License-Identifier: GPL-2.0-only
/*
 * Synaptics I2C RMI Touchscreen Driver (Mainline Port)
 * Originally derived from Google / HTC Hero / Mahimahi drivers.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/input/mt.h>

#define SYNAPTICS_RMI_NAME "synaptics_i2c_rmi"

struct synaptics_ts_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct touchscreen_properties props;
	struct regulator *vdd;
	struct regulator *vcc_i2c;
	bool has_relative_report;
	u8 sensitivity_adjust;
	u32 dup_threshold;
	int reported_finger_count;
};

static int synaptics_init_panel(struct synaptics_ts_data *ts)
{
	int ret;

	/* Page select = 0x10 */
	ret = i2c_smbus_write_byte_data(ts->client, 0xff, 0x10);
	if (ret < 0)
		return ret;

	/* Set "No Clip Z" */
	ret = i2c_smbus_write_byte_data(ts->client, 0x41, 0x04);
	if (ret < 0)
		dev_err(&ts->client->dev, "Failed to set No Clip Z\n");

	if (ts->sensitivity_adjust) {
		ret = i2c_smbus_write_byte_data(ts->client, 0x44, ts->sensitivity_adjust);
		if (ret < 0)
			dev_err(&ts->client->dev, "Failed to set Sensitivity Adjust\n");
	}

	/* Switch back to page select = 0x04 */
	ret = i2c_smbus_write_byte_data(ts->client, 0xff, 0x04);
	if (ret < 0)
		return ret;

	/* Normal operation: 80 reports per second */
	return i2c_smbus_write_byte_data(ts->client, 0xf0, 0x81);
}

static irqreturn_t synaptics_ts_irq_handler(int irq, void *dev_id)
{
	struct synaptics_ts_data *ts = dev_id;
	struct i2c_msg msg[2];
	u8 start_reg = 0x00;
	u8 buf[15];
	int buf_len = ts->has_relative_report ? 15 : 13;
	int ret;

	msg[0].addr = ts->client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &start_reg;

	msg[1].addr = ts->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = buf_len;
	msg[1].buf = buf;

	ret = i2c_transfer(ts->client->adapter, msg, 2);
	if (ret < 0) {
		dev_err(&ts->client->dev, "i2c_transfer failed in IRQ\n");
		return IRQ_HANDLED;
	}

	if ((buf[buf_len - 1] & 0xc0) != 0x40) {
		dev_warn(&ts->client->dev, "Bad packet status read: 0x%02x\n", buf[buf_len - 1]);
		return IRQ_HANDLED;
	}

	if ((buf[buf_len - 1] & 1) != 0) {
		int z = buf[1];
		int w = buf[0] >> 4;
		int finger = buf[0] & 7;
		int pos[2][2];
		int f, base = 2;
		bool finger2_pressed;

		for (f = 0; f < 2; f++) {
			pos[f][0] = buf[base + 1] | ((u16)(buf[base] & 0x1f) << 8);
			pos[f][1] = buf[base + 3] | ((u16)(buf[base + 2] & 0x1f) << 8);
			base += 6;
		}

		finger2_pressed = (finger > 1 && finger != 7);

		/* Report Slot 0 Single Touch */
		if (z) {
			touchscreen_report_pos(ts->input_dev, &ts->props, pos[0][0], pos[0][1], true);
			input_report_abs(ts->input_dev, ABS_PRESSURE, z);
			input_report_abs(ts->input_dev, ABS_TOOL_WIDTH, w);
		}
		input_report_key(ts->input_dev, BTN_TOUCH, finger > 0);
		input_report_key(ts->input_dev, BTN_2, finger2_pressed);

		/* Multi-touch events (MT Protocol A) */
		if (finger > 0) {
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, z);
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, w);
			touchscreen_report_pos(ts->input_dev, &ts->props, pos[0][0], pos[0][1], true);
			input_mt_sync(ts->input_dev);
		}

		if (finger2_pressed) {
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, z);
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, w);
			touchscreen_report_pos(ts->input_dev, &ts->props, pos[1][0], pos[1][1], true);
			input_mt_sync(ts->input_dev);
		} else if (ts->reported_finger_count > 1) {
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0);
			input_mt_sync(ts->input_dev);
		}

		ts->reported_finger_count = finger;
		input_sync(ts->input_dev);
	}

	return IRQ_HANDLED;
}

static int synaptics_ts_probe(struct i2c_client *client)
{
	struct synaptics_ts_data *ts;
	struct input_dev *input_dev;
	int ret;
	u16 max_x, max_y;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->client = client;
	i2c_set_clientdata(client, ts);

	/* Regulators replacing custom power function pointers */
	ts->vdd = devm_regulator_get_optional(&client->dev, "vdd");
	if (IS_ERR(ts->vdd)) {
		ret = PTR_ERR(ts->vdd);
		if (ret != -ENODEV)
			return ret;
		ts->vdd = NULL;
	}

	if (ts->vdd) {
		ret = regulator_enable(ts->vdd);
		if (ret) {
			dev_err(&client->dev, "Failed to enable vdd regulator\n");
			return ret;
		}
	}

	/* Reset IC */
	ret = i2c_smbus_write_byte_data(client, 0xf4, 0x01);
	if (ret < 0)
		dev_warn(&client->dev, "Device reset command failed\n");
	msleep(100);

	/* Check page properties */
	ret = i2c_smbus_write_byte_data(client, 0xff, 0x10);
	if (ret < 0)
		goto err_power;

	ret = i2c_smbus_read_word_data(client, 0x02);
	if (ret < 0)
		goto err_power;
	ts->has_relative_report = !(ret & 0x100);

	ret = i2c_smbus_read_word_data(client, 0x04);
	if (ret < 0)
		goto err_power;
	max_x = ((ret >> 8) & 0xff) | ((ret & 0x1f) << 8);

	ret = i2c_smbus_read_word_data(client, 0x06);
	if (ret < 0)
		goto err_power;
	max_y = ((ret >> 8) & 0xff) | ((ret & 0x1f) << 8);

	/* Parse Device Tree properties */
	device_property_read_u8(&client->dev, "synaptics,sensitivity-adjust", &ts->sensitivity_adjust);

	ret = synaptics_init_panel(ts);
	if (ret < 0)
		goto err_power;

	input_dev = devm_input_allocate_device(&client->dev);
	if (!input_dev) {
		ret = -ENOMEM;
		goto err_power;
	}

	ts->input_dev = input_dev;
	input_dev->name = "Synaptics Touchscreen";
	input_dev->id.bustype = BUS_I2C;

	input_set_capability(input_dev, EV_KEY, BTN_TOUCH);
	input_set_capability(input_dev, EV_KEY, BTN_2);

	input_set_abs_params(input_dev, ABS_X, 0, max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, max_y, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_TOOL_WIDTH, 0, 15, 0, 0);

	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, max_y, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, 15, 0, 0);

	/* Initialize MT Protocol A */
	ret = input_mt_init_slots(input_dev, 0, INPUT_MT_DIRECT);
	if (ret) {
		dev_err(&client->dev, "Failed to initialize MT slots\n");
		goto err_power;
	}

	/* Modern DT axis parsing: handles touchscreen-inverted-x, touchscreen-swapped-x-y, etc. */
	touchscreen_parse_properties(input_dev, true, &ts->props);

	ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
					synaptics_ts_irq_handler,
					IRQF_ONESHOT, client->name, ts);
	if (ret) {
		dev_err(&client->dev, "Failed to request threaded IRQ\n");
		goto err_power;
	}

	/* Enable ABS IRQ */
	i2c_smbus_write_byte_data(client, 0xf1, 0x01);

	ret = input_register_device(ts->input_dev);
	if (ret)
		goto err_power;

	return 0;

err_power:
	if (ts->vdd)
		regulator_disable(ts->vdd);
	return ret;
}

static void synaptics_ts_remove(struct i2c_client *client)
{
	struct synaptics_ts_data *ts = i2c_get_clientdata(client);

	i2c_smbus_write_byte_data(client, 0xf1, 0x00); /* Disable interrupt */
	i2c_smbus_write_byte_data(client, 0xf0, 0x86); /* Deep sleep */

	if (ts->vdd)
		regulator_disable(ts->vdd);
}

static const struct of_device_id synaptics_ts_of_match[] = {
	{ .compatible = "synaptics,synaptics_rmi" },
	{ }
};
MODULE_DEVICE_TABLE(of, synaptics_ts_of_match);

static const struct i2c_device_id synaptics_ts_id[] = {
	{ SYNAPTICS_RMI_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, synaptics_ts_id);

static struct i2c_driver synaptics_ts_driver = {
	.driver = {
		.name = SYNAPTICS_RMI_NAME,
		.of_match_table = synaptics_ts_of_match,
	},
	.probe = synaptics_ts_probe,
	.remove = synaptics_ts_remove,
	.id_table = synaptics_ts_id,
};

module_i2c_driver(synaptics_ts_driver);

MODULE_AUTHOR("Google, Inc.");
MODULE_DESCRIPTION("Synaptics Touchscreen Mainline Driver");
MODULE_LICENSE("GPL v2");