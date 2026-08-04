// SPDX-License-Identifier: GPL-2.0
/*
 * Temporary HTC QSD8250 Bluetooth UART pin mux driver
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/mach-msm/msm-proc_comm.h>

#define GPIO_BT_UART1_RTS 43
#define GPIO_BT_UART1_CTS 44
#define GPIO_BT_UART1_RX  45
#define GPIO_BT_UART1_TX  46

static uint32_t wifi_on_gpio_table[] = {
	PCOM_GPIO_CFG(51, 1, PCOM_GPIO_CFG_OUTPUT, PCOM_GPIO_CFG_PULL_UP, PCOM_GPIO_CFG_4MA), /* DAT3 */
	PCOM_GPIO_CFG(52, 1, PCOM_GPIO_CFG_OUTPUT, PCOM_GPIO_CFG_PULL_UP, PCOM_GPIO_CFG_4MA), /* DAT2 */
	PCOM_GPIO_CFG(53, 1, PCOM_GPIO_CFG_OUTPUT, PCOM_GPIO_CFG_PULL_UP, PCOM_GPIO_CFG_4MA), /* DAT1 */
	PCOM_GPIO_CFG(54, 1, PCOM_GPIO_CFG_OUTPUT, PCOM_GPIO_CFG_PULL_UP, PCOM_GPIO_CFG_4MA), /* DAT0 */
	PCOM_GPIO_CFG(55, 1, PCOM_GPIO_CFG_OUTPUT, PCOM_GPIO_CFG_PULL_UP, PCOM_GPIO_CFG_8MA), /* CMD */
	PCOM_GPIO_CFG(56, 1, PCOM_GPIO_CFG_OUTPUT, PCOM_GPIO_CFG_NO_PULL, PCOM_GPIO_CFG_8MA), /* CLK */
	PCOM_GPIO_CFG(152, 0, PCOM_GPIO_CFG_INPUT, PCOM_GPIO_CFG_NO_PULL, PCOM_GPIO_CFG_4MA),  /* WLAN IRQ */
};

static int htc_bt_gpio_init(void)
{
	static const unsigned configs[] = {
		/* UART1 */
		PCOM_GPIO_CFG(43, 2, PCOM_GPIO_CFG_OUTPUT,
			      PCOM_GPIO_CFG_PULL_UP, PCOM_GPIO_CFG_8MA),

		PCOM_GPIO_CFG(44, 2, PCOM_GPIO_CFG_INPUT,
			      PCOM_GPIO_CFG_PULL_UP, PCOM_GPIO_CFG_8MA),

		PCOM_GPIO_CFG(45, 2, PCOM_GPIO_CFG_INPUT,
			      PCOM_GPIO_CFG_PULL_UP, PCOM_GPIO_CFG_8MA),

		PCOM_GPIO_CFG(46, 2, PCOM_GPIO_CFG_OUTPUT,
			      PCOM_GPIO_CFG_PULL_UP, PCOM_GPIO_CFG_8MA),

		/* BT reset/shutdown */
		PCOM_GPIO_CFG(146, 0,PCOM_GPIO_CFG_OUTPUT,
			      PCOM_GPIO_CFG_PULL_DOWN, PCOM_GPIO_CFG_4MA),

		PCOM_GPIO_CFG(128, 0,PCOM_GPIO_CFG_OUTPUT,
			      PCOM_GPIO_CFG_PULL_DOWN, PCOM_GPIO_CFG_4MA),

		/* wake lines */
		PCOM_GPIO_CFG(127, 0, PCOM_GPIO_CFG_OUTPUT,
			      PCOM_GPIO_CFG_NO_PULL, PCOM_GPIO_CFG_2MA),

		PCOM_GPIO_CFG(86, 0, PCOM_GPIO_CFG_INPUT,
			      PCOM_GPIO_CFG_PULL_DOWN, PCOM_GPIO_CFG_4MA),
	};

	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(configs); i++) {
		ret = pcom_gpio_tlmm_config(configs[i], GPIO_ENABLE);
		if (ret)
			pr_err("bt gpio cfg %d failed: %d\n", i, ret);
	}
	for (i = 0; i < ARRAY_SIZE(wifi_on_gpio_table); i++) {
		ret = pcom_gpio_tlmm_config(wifi_on_gpio_table[i], GPIO_ENABLE);
		if (ret)
			pr_err("bt gpio cfg %d failed: %d\n", i, ret);
	}

	return 0;
}

static int qsd8250_bt_uart_pins_probe(struct platform_device *pdev)
{
	int ret;

	pr_info("qsd8250-bt-uart-pins: configuring UART1 pins\n");

	ret = pcom_gpio_tlmm_config(
		PCOM_GPIO_CFG(GPIO_BT_UART1_RTS, 2,
			PCOM_GPIO_CFG_OUTPUT,
			PCOM_GPIO_CFG_PULL_UP,
			GPIO_4MA),
		PCOM_GPIO_CFG_ENABLE);
	if (ret)
		return ret;

	ret = pcom_gpio_tlmm_config(
		PCOM_GPIO_CFG(GPIO_BT_UART1_CTS, 2,
			PCOM_GPIO_CFG_INPUT,
			PCOM_GPIO_CFG_PULL_UP,
			GPIO_4MA),
		PCOM_GPIO_CFG_ENABLE);
	if (ret)
		return ret;

	ret = pcom_gpio_tlmm_config(
		PCOM_GPIO_CFG(GPIO_BT_UART1_RX, 2,
			PCOM_GPIO_CFG_INPUT,
			PCOM_GPIO_CFG_PULL_UP,
			GPIO_4MA),
		PCOM_GPIO_CFG_ENABLE);
	if (ret)
		return ret;

	ret = pcom_gpio_tlmm_config(
		PCOM_GPIO_CFG(GPIO_BT_UART1_TX, 2,
			PCOM_GPIO_CFG_OUTPUT,
			PCOM_GPIO_CFG_PULL_UP,
			GPIO_4MA),
		PCOM_GPIO_CFG_ENABLE);

	if (ret)
		return ret;

	ret = htc_bt_gpio_init();
	if (ret)
		return ret;

	pr_info("qsd8250-bt-uart-pins: UART1 mux enabled\n");

	return 0;
}

static const struct of_device_id qsd8250_bt_uart_pins_of_match[] = {
	{ .compatible = "htc,qsd8250-bt-uart-pins" },
	{}
};
MODULE_DEVICE_TABLE(of, qsd8250_bt_uart_pins_of_match);

static struct platform_driver qsd8250_bt_uart_pins_driver = {
	.driver = {
		.name = "qsd8250-bt-uart-pins",
		.of_match_table = qsd8250_bt_uart_pins_of_match,
	},
	.probe = qsd8250_bt_uart_pins_probe,
};

module_platform_driver(qsd8250_bt_uart_pins_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HTC QSD8250 Bluetooth UART pin mux");