// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm proc_comm USB reset provider
 *
 * On pre-GPIO-reset-controller Qualcomm SoCs (QSD8250 and similar),
 * the USB link and PHY blocks can only be reset via an RPC call to
 * the modem processor (proc_comm), not through any Linux-visible
 * register. This exposes that sequence as a single reset line so
 * consumer drivers (e.g. phy-qcom-usb-hs.c) can request it generically
 * via reset_control_reset() instead of calling msm_proc_comm() directly.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/mach-msm/msm-proc_comm.h>

#define PCOM_RESET_USBH		37
#define PCOM_RESET_USB_PHY	34

static int pcom_reset_id(u32 id)
{
	u32 num = id;

	return msm_proc_comm(PCOM_CLK_REGIME_SEC_RESET_ASSERT, &num, NULL);
}

static int pcom_deassert_id(u32 id)
{
	u32 num = id;

	return msm_proc_comm(PCOM_CLK_REGIME_SEC_RESET_DEASSERT, &num, NULL);
}

static int qcom_pcom_usb_reset(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	int ret;

	ret = pcom_reset_id(PCOM_RESET_USBH);
	if (ret)
		return ret;

	ret = pcom_reset_id(PCOM_RESET_USB_PHY);
	if (ret)
		return ret;

	msleep(1);

	ret = pcom_deassert_id(PCOM_RESET_USB_PHY);
	if (ret)
		return ret;

	return pcom_deassert_id(PCOM_RESET_USBH);
}

static const struct reset_control_ops qcom_pcom_usb_reset_ops = {
	.reset = qcom_pcom_usb_reset,
};

static int qcom_pcom_usb_reset_xlate(struct reset_controller_dev *rcdev,
				      const struct of_phandle_args *spec)
{
	return 0; /* single fixed line, #reset-cells = <0> */
}

static struct reset_controller_dev pcom_rcdev = {
	.ops		= &qcom_pcom_usb_reset_ops,
	.owner		= THIS_MODULE,
	.nr_resets	= 1,
	.of_xlate	= qcom_pcom_usb_reset_xlate,
};

static int qcom_pcom_usb_reset_probe(struct platform_device *pdev)
{
	pcom_rcdev.of_node = pdev->dev.of_node;
	pcom_rcdev.of_reset_n_cells = 0;

	return devm_reset_controller_register(&pdev->dev, &pcom_rcdev);
}

static const struct of_device_id qcom_pcom_usb_reset_match[] = {
	{ .compatible = "qcom,pcom-usb-reset" },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_pcom_usb_reset_match);

static struct platform_driver qcom_pcom_usb_reset_driver = {
	.probe = qcom_pcom_usb_reset_probe,
	.driver = {
		.name = "qcom-pcom-usb-reset",
		.of_match_table = qcom_pcom_usb_reset_match,
	},
};
module_platform_driver(qcom_pcom_usb_reset_driver);

MODULE_DESCRIPTION("Qualcomm proc_comm USB reset provider");
MODULE_LICENSE("GPL");