// SPDX-License-Identifier: GPL-2.0-only
/*
 * Regulator driver for the Qualcomm qsd8250 soc
 *
 * Ported from the downstream vreg driver:
 * Copyright (C) 2008 Google, Inc.
 * Copyright (c) 2009, Code Aurora Forum. All rights reserved.
 * Author: Brian Swetland <swetland@google.com>
 * to the mainline Linux regulator subsystem.
 *
 * Copyright (C) 2026 J0SH1X <aljoshua.hell@gmail.com>
 */

#include <linux/limits.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#include <linux/mach-msm/msm-proc_comm.h>

struct qsd_vreg {
	struct regulator_desc desc;

	unsigned int id;
	unsigned int refcnt;

	unsigned int min_uV;
	unsigned int max_uV;

	unsigned int cur_uV;

	struct mutex lock;
};

struct qsd_vreg_drv {
	struct device *dev;
	struct qsd_vreg *vregs;
	int count;
};

static int qsd_vreg_enable(struct regulator_dev *rdev)
{
	struct qsd_vreg *vreg = rdev_get_drvdata(rdev);
	unsigned int id;
	unsigned int enable = 1;
	int ret = 0;

	mutex_lock(&vreg->lock);

	id = vreg->id;

	if (!vreg->refcnt)
		ret = msm_proc_comm(PCOM_VREG_SWITCH, &id, &enable);

	if (!ret)
		vreg->refcnt++;

	mutex_unlock(&vreg->lock);

	return ret;
}

static int qsd_vreg_disable(struct regulator_dev *rdev)
{
	struct qsd_vreg *vreg = rdev_get_drvdata(rdev);
	unsigned int id;
	unsigned int enable = 0;
	int ret = 0;

	mutex_lock(&vreg->lock);

	if (!vreg->refcnt)
		goto out;

	id = vreg->id;

	if (vreg->refcnt == 1)
		ret = msm_proc_comm(PCOM_VREG_SWITCH, &id, &enable);

	if (!ret)
		vreg->refcnt--;

out:
	mutex_unlock(&vreg->lock);

	return ret;
}

static int qsd_vreg_get_voltage_sel(struct regulator_dev *rdev)
{
	struct qsd_vreg *vreg = rdev_get_drvdata(rdev);

	return (vreg->cur_uV - vreg->desc.min_uV) /
		vreg->desc.uV_step;
}

static int qsd_vreg_set_voltage_sel(struct regulator_dev *rdev,
				    unsigned int sel)
{
	struct qsd_vreg *vreg = rdev_get_drvdata(rdev);
	unsigned int id = vreg->id;
	unsigned int mv;
	unsigned int uv;

	uv = vreg->desc.min_uV +
	     sel * vreg->desc.uV_step;

	mv = DIV_ROUND_CLOSEST(uv, 1000);

	if (msm_proc_comm(PCOM_VREG_SET_LEVEL, &id, &mv))
		return -EIO;

	vreg->cur_uV = uv;

	return 0;
}

static const struct regulator_ops qsd_vreg_ops = {
	.enable = qsd_vreg_enable,
	.disable = qsd_vreg_disable,

	.set_voltage_sel = qsd_vreg_set_voltage_sel,
	.get_voltage_sel = qsd_vreg_get_voltage_sel,

	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
};

static int qsd8250_vreg_probe(struct platform_device *pdev)
{
	struct device_node *child;
	struct qsd_vreg_drv *drv;
	int count;
	int i = 0;

	count = of_get_child_count(pdev->dev.of_node);

	if (!count)
		return -ENODEV;

	drv = devm_kzalloc(&pdev->dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	drv->vregs = devm_kcalloc(&pdev->dev, count,
				   sizeof(*drv->vregs),
				   GFP_KERNEL);
	if (!drv->vregs)
		return -ENOMEM;

	drv->count = count;

	for_each_child_of_node(pdev->dev.of_node, child) {
		struct qsd_vreg *vreg = &drv->vregs[i];
		struct regulator_config cfg = {};
		struct regulator_dev *rdev;

		vreg->id = 0;

		of_property_read_u32(child,
				     "qcom,vreg-id",
				     &vreg->id);

mutex_init(&vreg->lock);

of_property_read_u32(child,
		     "regulator-min-microvolt",
		     &vreg->min_uV);

of_property_read_u32(child,
		     "regulator-max-microvolt",
		     &vreg->max_uV);

if (!vreg->min_uV)
	vreg->min_uV = 750000;

if (!vreg->max_uV)
	vreg->max_uV = 3300000;

vreg->cur_uV = vreg->min_uV;

vreg->desc.name = child->name;
vreg->desc.id = i;
vreg->desc.ops = &qsd_vreg_ops;
vreg->desc.type = REGULATOR_VOLTAGE;

vreg->desc.min_uV = vreg->min_uV;
vreg->desc.uV_step = 1000;
vreg->desc.n_voltages =
	(vreg->max_uV - vreg->min_uV) / 1000 + 1;

vreg->desc.owner = THIS_MODULE;

cfg.dev = &pdev->dev;
cfg.driver_data = vreg;
cfg.of_node = child;

dev_info(&pdev->dev,
	 "%s: id=%u voltage=%u-%uuV steps=%u\n",
	 vreg->desc.name,
	 vreg->id,
	 vreg->min_uV,
	 vreg->max_uV,
	 vreg->desc.n_voltages);

		rdev = devm_regulator_register(&pdev->dev,&vreg->desc,&cfg);

		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev,"failed registering %s\n",child->name);
			return PTR_ERR(rdev);
		}

		i++;
	}

	platform_set_drvdata(pdev, drv);

	dev_info(&pdev->dev,
		 "registered %d QSD8250 regulators\n",
		 count);

	return 0;
}

static const struct of_device_id qsd8250_vreg_of_match[] = {
	{
		.compatible = "qcom,qsd8250-vreg",
	},
	{}
};

MODULE_DEVICE_TABLE(of, qsd8250_vreg_of_match);

static struct platform_driver qsd8250_vreg_driver = {
	.probe = qsd8250_vreg_probe,
	.driver = {
		.name = "qsd8250-vreg",
		.of_match_table = qsd8250_vreg_of_match,
	},
};

module_platform_driver(qsd8250_vreg_driver);

MODULE_AUTHOR("J0SH1X <aljoshua.hell@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("QSD8250 proc_comm DT regulator driver");