// SPDX-License-Identifier: GPL-2.0-only
/*
 * HTC jogball (trackball) input driver - HTC Passion / Nexus One (mahimahi).
 *
 * The jogball reports movement as two mechanical quadrature pairs, one
 * per axis: the "left"/"right" lines are the A/B phases of the X axis,
 * the "up"/"down" lines the A/B phases of the Y axis. Neither line on
 * its own encodes a direction - both toggle while the ball is rolling,
 * whichever way it rolls. The direction is only in the *order* the two
 * phases change in, so it has to be decoded from the transition between
 * the old and the new (A, B) state.
 *
 * The downstream driver (board-mahimahi-keypad.c) did this decoding via
 * the generic gpio_event_axis framework and emitted relative REL_X/REL_Y
 * motion. This driver keeps the decoding but reports discrete arrow keys
 * instead, so the input interface is EV_KEY with KEY_UP/DOWN/LEFT/RIGHT.
 *
 * The one piece of downstream logic that is kept here verbatim: encoder
 * glitches right after power-up are ignored for the first couple of
 * jiffies (jog_just_on / jog_on_jiffies upstream).
 *
 * Ported from:
 * Copyright (C) 2009 Google, Inc
 * Copyright (C) 2009 HTC Corporation.
 * Author: Dima Zavin <dima@android.com>
 *
 * Copyright (C) 2024 (mainline port)
 */

#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/spinlock.h>

#define HTC_JOGBALL_PHASES		2

/*
 * Quadrature steps to accumulate before emitting one key press. A full
 * cycle of the encoder is four steps; two gives one key per half cycle,
 * which matches the key repeat rate the ball is comfortably driven at.
 */
#define HTC_JOGBALL_STEPS_PER_KEY	2

/*
 * Gray code transition table, indexed by (old_state << 2) | new_state
 * with state == (phase_a << 1) | phase_b. Valid single-step transitions
 * yield +-1, no change and invalid (both phases changed at once, i.e. a
 * missed edge) yield 0.
 */
static const s8 htc_jogball_qdec[16] = {
	 0, -1,  1,  0,
	 1,  0,  0, -1,
	-1,  0,  0,  1,
	 0,  1, -1,  0,
};

struct htc_jogball;

struct htc_jogball_axis {
	struct htc_jogball *jb;
	struct gpio_desc *gpio[HTC_JOGBALL_PHASES];
	unsigned int keycode_neg;
	unsigned int keycode_pos;

	/* serialises the two per-phase interrupts of this axis */
	spinlock_t lock;
	u8 state;
	s8 accum;
};

struct htc_jogball {
	struct device *dev;
	struct input_dev *input;
	struct regulator *vreg;

	struct htc_jogball_axis x;
	struct htc_jogball_axis y;

	bool just_on;
	unsigned long on_jiffies;
};

static u8 htc_jogball_read_state(struct htc_jogball_axis *axis)
{
	return gpiod_get_value_cansleep(axis->gpio[0]) << 1 |
	       gpiod_get_value_cansleep(axis->gpio[1]);
}

/*
 * Right after power-up the ball's encoders can glitch and report a
 * bogus direction. Downstream ignored any event landing in the same or
 * very next jiffy after power-on; replicated here.
 */
static bool htc_jogball_should_ignore(struct htc_jogball *jb)
{
	if (!jb->just_on)
		return false;

	if (time_before_eq(jiffies, jb->on_jiffies + 1))
		return true;

	jb->just_on = false;
	return false;
}

static irqreturn_t htc_jogball_irq(int irq, void *data)
{
	struct htc_jogball_axis *axis = data;
	struct htc_jogball *jb = axis->jb;
	unsigned int keycode;
	unsigned long flags;
	u8 state;
	s8 delta;

	state = htc_jogball_read_state(axis);

	spin_lock_irqsave(&axis->lock, flags);

	delta = htc_jogball_qdec[axis->state << 2 | state];
	axis->state = state;

	/* no movement, or a missed edge we cannot assign a direction to */
	if (!delta || htc_jogball_should_ignore(jb)) {
		axis->accum = 0;
		spin_unlock_irqrestore(&axis->lock, flags);
		return IRQ_HANDLED;
	}

	/* a reversal discards whatever was accumulated the other way */
	if ((axis->accum > 0) != (delta > 0))
		axis->accum = 0;
	axis->accum += delta;

	if (abs(axis->accum) < HTC_JOGBALL_STEPS_PER_KEY) {
		spin_unlock_irqrestore(&axis->lock, flags);
		return IRQ_HANDLED;
	}

	keycode = axis->accum > 0 ? axis->keycode_pos : axis->keycode_neg;
	axis->accum = 0;

	spin_unlock_irqrestore(&axis->lock, flags);

	input_report_key(jb->input, keycode, 1);
	input_sync(jb->input);
	input_report_key(jb->input, keycode, 0);
	input_sync(jb->input);

	return IRQ_HANDLED;
}

static int htc_jogball_open(struct input_dev *input)
{
	struct htc_jogball *jb = input_get_drvdata(input);
	int ret;

	ret = regulator_enable(jb->vreg);
	if (ret)
		return ret;

	/* matches jog_just_on/jog_on_jiffies being (re)armed on every
	 * power-on in the downstream jogball_power() callback
	 */
	jb->just_on = true;
	jb->on_jiffies = jiffies;

	/* seed the decoder so the first edge is decoded against the real
	 * line state rather than against a stale one
	 */
	jb->x.state = htc_jogball_read_state(&jb->x);
	jb->y.state = htc_jogball_read_state(&jb->y);
	jb->x.accum = 0;
	jb->y.accum = 0;

	return 0;
}

static void htc_jogball_close(struct input_dev *input)
{
	struct htc_jogball *jb = input_get_drvdata(input);

	regulator_disable(jb->vreg);
}

static int htc_jogball_setup_phase(struct platform_device *pdev,
				   struct htc_jogball_axis *axis,
				   unsigned int phase, const char *con_id)
{
	struct device *dev = &pdev->dev;
	int irq, ret;

	axis->gpio[phase] = devm_gpiod_get(dev, con_id, GPIOD_IN);
	if (IS_ERR(axis->gpio[phase]))
		return dev_err_probe(dev, PTR_ERR(axis->gpio[phase]),
				     "failed to get %s-gpios\n", con_id);

	irq = gpiod_to_irq(axis->gpio[phase]);
	if (irq < 0)
		return dev_err_probe(dev, irq,
				     "failed to get irq for %s-gpios\n", con_id);

	ret = devm_request_threaded_irq(dev, irq, NULL, htc_jogball_irq,
					IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					con_id, axis);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to request irq for %s-gpios\n",
				     con_id);

	return 0;
}

/*
 * @con_id_a / @con_id_b are the two phases of one axis. Their order and
 * the keycode assignment together define the sign of the axis; swapping
 * either one inverts the reported direction.
 */
static int htc_jogball_setup_axis(struct platform_device *pdev,
				  struct htc_jogball *jb,
				  struct htc_jogball_axis *axis,
				  const char *con_id_a, const char *con_id_b,
				  unsigned int keycode_neg,
				  unsigned int keycode_pos)
{
	int ret;

	axis->jb = jb;
	axis->keycode_neg = keycode_neg;
	axis->keycode_pos = keycode_pos;
	spin_lock_init(&axis->lock);

	ret = htc_jogball_setup_phase(pdev, axis, 0, con_id_a);
	if (ret)
		return ret;

	ret = htc_jogball_setup_phase(pdev, axis, 1, con_id_b);
	if (ret)
		return ret;

	input_set_capability(jb->input, EV_KEY, keycode_neg);
	input_set_capability(jb->input, EV_KEY, keycode_pos);

	return 0;
}

static int htc_jogball_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct htc_jogball *jb;
	int ret;

	jb = devm_kzalloc(dev, sizeof(*jb), GFP_KERNEL);
	if (!jb)
		return -ENOMEM;

	jb->dev = dev;

	/* UMTS/Passion powers the jogball off the "gp2" PMIC rail; the CDMA
	 * variant instead gates a plain GPIO (MAHIMAHI_CDMA_JOG_2V6_EN in
	 * the downstream source) - not wired up here since this is the
	 * UMTS/Passion board.
	 */
	jb->vreg = devm_regulator_get(dev, "vcc");
	if (IS_ERR(jb->vreg))
		return dev_err_probe(dev, PTR_ERR(jb->vreg),
				     "failed to get vcc-supply\n");

	jb->input = devm_input_allocate_device(dev);
	if (!jb->input)
		return -ENOMEM;

	jb->input->name = "htc-jogball";
	jb->input->phys = "htc-jogball/input0";
	jb->input->open = htc_jogball_open;
	jb->input->close = htc_jogball_close;
	input_set_drvdata(jb->input, jb);

	ret = htc_jogball_setup_axis(pdev, jb, &jb->x, "left", "right",
				     KEY_LEFT, KEY_RIGHT);
	if (ret)
		return ret;

	ret = htc_jogball_setup_axis(pdev, jb, &jb->y, "up", "down",
				     KEY_UP, KEY_DOWN);
	if (ret)
		return ret;

	ret = input_register_device(jb->input);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register input device\n");

	return 0;
}

static const struct of_device_id htc_jogball_of_match[] = {
	{ .compatible = "htc,jogball" },
	{ }
};
MODULE_DEVICE_TABLE(of, htc_jogball_of_match);

static struct platform_driver htc_jogball_driver = {
	.driver = {
		.name = "htc-jogball",
		.of_match_table = htc_jogball_of_match,
	},
	.probe = htc_jogball_probe,
};
module_platform_driver(htc_jogball_driver);

MODULE_DESCRIPTION("HTC jogball (trackball) input driver");
MODULE_LICENSE("GPL v2");