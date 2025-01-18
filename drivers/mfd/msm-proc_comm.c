/* arch/arm/mach-msm/proc_comm.c
 *
 * Copyright (C) 2007-2008 Google, Inc.
 * Copyright (c) 2009-2011, Code Aurora Forum. All rights reserved.
 * Copyright (c) 2025, HtcLeoRevivalProject.
 * Author: Brian Swetland <swetland@google.com>
 * Author: J0SH1X <aljoshua.hell@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/mach-msm/msm-iomap-qsd8k.h>

#include <linux/mach-msm/msm-proc_comm.h>

#define APP_COMMAND 0x00
#define APP_STATUS  0x04
#define APP_DATA1   0x08
#define APP_DATA2   0x0C

#define MDM_COMMAND 0x10
#define MDM_STATUS  0x14
#define MDM_DATA1   0x18
#define MDM_DATA2   0x1C

#define PCOM_VREG_SDC PM_VREG_PDOWN_GP6_ID //?
#define PCOM_MPP_FOR_USB_VBUS PM_MPP_16 //?
#define PROC_COMM_END_CMDS 0xFFFF 

static DEFINE_SPINLOCK(proc_comm_lock);
static int msm_proc_comm_disable;

struct msm_proc_comm_data {
    void __iomem *shared_ram_base;
    void __iomem *csr_base;
};

static struct msm_proc_comm_data *proc_comm_data;

static inline void notify_other_proc_comm(void)
{
    /* Make sure the write completes before interrupt */
    wmb();

    __raw_writel(1, proc_comm_data->csr_base + 0x400 + (6) * 4);
}

/* Poll for a state change, checking for possible
 * modem crashes along the way (so we don't wait
 * forever while the ARM9 is blowing up.
 *
 * Return an error in the event of a modem crash and
 * restart so the msm_proc_comm() routine can restart
 * the operation from the beginning.
 */
static int proc_comm_wait_for(void __iomem *addr, unsigned value)
{
    while (1) {
        /* Barrier here prevents excessive spinning */
        mb();
        if (readl_relaxed(addr) == value)
            return 0;
        //j0sh1x: we dont have any smsm driver yet
        // if (smsm_check_for_modem_crash())
        //     return -EAGAIN;

        udelay(5);
    }
}

void msm_proc_comm_reset_modem_now(void)
{
    unsigned long flags;

    spin_lock_irqsave(&proc_comm_lock, flags);

again:
    if (proc_comm_wait_for(proc_comm_data->shared_ram_base + MDM_STATUS, PCOM_READY))
        goto again;

    writel_relaxed(PCOM_RESET_MODEM, proc_comm_data->shared_ram_base + APP_COMMAND);
    writel_relaxed(0, proc_comm_data->shared_ram_base + APP_DATA1);
    writel_relaxed(0, proc_comm_data->shared_ram_base + APP_DATA2);

    spin_unlock_irqrestore(&proc_comm_lock, flags);

    /* Make sure the writes complete before notifying the other side */
    wmb();
    notify_other_proc_comm();

    return;
}
EXPORT_SYMBOL(msm_proc_comm_reset_modem_now);

int msm_proc_comm(unsigned cmd, unsigned *data1, unsigned *data2)
{
    unsigned long flags;
    int ret;

    spin_lock_irqsave(&proc_comm_lock, flags);

    if (msm_proc_comm_disable) {
        ret = -EIO;
        goto end;
    }

again:
    if (proc_comm_wait_for(proc_comm_data->shared_ram_base + MDM_STATUS, PCOM_READY))
        goto again;

    writel_relaxed(cmd, proc_comm_data->shared_ram_base + APP_COMMAND);
    writel_relaxed(data1 ? *data1 : 0, proc_comm_data->shared_ram_base + APP_DATA1);
    writel_relaxed(data2 ? *data2 : 0, proc_comm_data->shared_ram_base + APP_DATA2);

    /* Make sure the writes complete before notifying the other side */
    wmb();
    notify_other_proc_comm();

    if (proc_comm_wait_for(proc_comm_data->shared_ram_base + APP_COMMAND, PCOM_CMD_DONE))
        goto again;

    if (readl_relaxed(proc_comm_data->shared_ram_base + APP_STATUS) == PCOM_CMD_SUCCESS) {
        if (data1)
            *data1 = readl_relaxed(proc_comm_data->shared_ram_base + APP_DATA1);
        if (data2)
            *data2 = readl_relaxed(proc_comm_data->shared_ram_base + APP_DATA2);
        ret = 0;
    } else {
        ret = -EIO;
    }

    writel_relaxed(PCOM_CMD_IDLE, proc_comm_data->shared_ram_base + APP_COMMAND);

    switch (cmd) {
    case PCOM_RESET_CHIP:
    case PCOM_RESET_CHIP_IMM:
    case PCOM_RESET_APPS:
#if 1
        /* Do not disable proc_comm when device reset */
#else
        msm_proc_comm_disable = 1;
        printk(KERN_ERR "msm: proc_comm: proc comm disabled\n");
#endif
        break;
    }
end:
    /* Make sure the writes complete before returning */
    wmb();
    spin_unlock_irqrestore(&proc_comm_lock, flags);
    return ret;
}
EXPORT_SYMBOL(msm_proc_comm);


int pcom_gpio_tlmm_config(unsigned config, unsigned disable)
{
	return msm_proc_comm(PCOM_RPC_GPIO_TLMM_CONFIG_EX, &config, &disable);
}

EXPORT_SYMBOL(pcom_gpio_tlmm_config);

int pcom_vreg_set_level(unsigned id, unsigned mv)
{
	return msm_proc_comm(PCOM_VREG_SET_LEVEL, &id, &mv);
}
EXPORT_SYMBOL(pcom_vreg_set_level);

int pcom_vreg_enable(unsigned id)
{
	unsigned int enable = 1;
	return msm_proc_comm(PCOM_VREG_SWITCH, &id, &enable);
}
EXPORT_SYMBOL(pcom_vreg_enable);

int pcom_vreg_disable(unsigned id)
{
	unsigned int enable = 0;
	return msm_proc_comm(PCOM_VREG_SWITCH, &id, &enable);
}
EXPORT_SYMBOL(pcom_vreg_disable);

int pcom_clock_enable(unsigned id)
{
	return msm_proc_comm(PCOM_CLKCTL_RPC_ENABLE, &id, 0);
}
EXPORT_SYMBOL(pcom_clock_enable);

int pcom_clock_disable(unsigned id)
{
	return msm_proc_comm(PCOM_CLKCTL_RPC_DISABLE, &id, 0);
}
EXPORT_SYMBOL(pcom_clock_disable);

int pcom_clock_is_enabled(unsigned id)
{
	return msm_proc_comm(PCOM_CLKCTL_RPC_ENABLED, &id, 0);
}
EXPORT_SYMBOL(pcom_clock_is_enabled);

int pcom_clock_set_rate(unsigned id, unsigned rate)
{
	return msm_proc_comm(PCOM_CLKCTL_RPC_SET_RATE, &id, &rate);
}
EXPORT_SYMBOL(pcom_clock_set_rate);

int pcom_clock_get_rate(unsigned id)
{
	if (msm_proc_comm(PCOM_CLKCTL_RPC_RATE, &id, 0)) {
		return -1;
	} else {
		return (int)id;
	}
}
EXPORT_SYMBOL(pcom_clock_get_rate);


int pcom_set_clock_flags(unsigned id, unsigned flags)
{
	return msm_proc_comm(PCOM_CLKCTL_RPC_SET_FLAGS, &id, &flags);
}
EXPORT_SYMBOL(pcom_set_clock_flags);

void pcom_vreg_control(unsigned vreg, unsigned level, unsigned state)
{
    unsigned s = (state ? PCOM_ENABLE : PCOM_DISABLE);

	/* If turning it ON, set the level first. */
    if(state)
    {
		do
        {
            msm_proc_comm(PCOM_VREG_SET_LEVEL, &vreg, &level);
		}while(PCOM_CMD_SUCCESS != readl(APP_STATUS));
    }
	
	do
    {
        msm_proc_comm(PCOM_VREG_SWITCH, &vreg, &s);
    }while(PCOM_CMD_SUCCESS != readl(APP_STATUS));
}
EXPORT_SYMBOL(pcom_vreg_control);

void pcom_sdcard_power(int state)
{
    unsigned v = PCOM_VREG_SDC;
	unsigned s = (state ? PCOM_ENABLE : PCOM_DISABLE);

	while(1)
	{
		msm_proc_comm(PCOM_VREG_SWITCH, &v, &s);

        if(PCOM_CMD_SUCCESS != readl(APP_STATUS)) {
			printk(KERN_INFO "Error: PCOM_VREG_SWITCH failed...retrying\n");
		} else {
			printk(KERN_INFO "PCOM_VREG_SWITCH DONE\n");
			break;
		}
    }
}
EXPORT_SYMBOL(pcom_sdcard_power);

void pcom_sdcard_gpio_config(int instance)
{
	switch (instance) {
		case 1:
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(51, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_16MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(52, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_16MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(53, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_16MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(54, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_16MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(55, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_16MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(56, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_NO_PULL, MSM_GPIO_CFG_16MA), MSM_GPIO_CFG_ENABLE);
			break;

		case 2:
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(62, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_NO_PULL, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(63, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(64, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(65, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(66, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(67, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			break;

		case 3:
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(88, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_NO_PULL, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(89, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(90, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(91, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(92, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(93, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(158, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(159, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(160, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(161, 1, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			break;

		case 4:
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(142, 3, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_NO_PULL, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(143, 3, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(144, 2, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(145, 2, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(146, 3, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			pcom_gpio_tlmm_config(MSM_GPIO_CFG(147, 3, MSM_GPIO_CFG_OUTPUT, MSM_GPIO_CFG_PULL_UP, MSM_GPIO_CFG_8MA), MSM_GPIO_CFG_ENABLE);
			break;
    }
}
EXPORT_SYMBOL(pcom_sdcard_gpio_config);

void pcom_usb_vbus_power(int state)
{
    unsigned v = PCOM_MPP_FOR_USB_VBUS;
	unsigned s = (PM_MPP__DLOGIC__LVL_VDD << 16) | (state ? PM_MPP__DLOGIC_OUT__CTRL_HIGH : PM_MPP__DLOGIC_OUT__CTRL_LOW);

	msm_proc_comm(PCOM_PM_MPP_CONFIG, &v, &s);

    if(PCOM_CMD_SUCCESS != readl(APP_STATUS)) {
        printk(KERN_INFO "Error: PCOM_MPP_CONFIG failed... not retrying\n");
    } else {
        printk(KERN_INFO "PCOM_MPP_CONFIG DONE\n");
    }
}
EXPORT_SYMBOL(pcom_usb_vbus_power);

void pcom_usb_reset_phy(void)
{
	while(1)
	{
        msm_proc_comm(PCOM_MSM_HSUSB_PHY_RESET, 0, 0);

        if(PCOM_CMD_SUCCESS != readl(APP_STATUS)) {
            printk(KERN_INFO "Error: PCOM_MSM_HSUSB_PHY_RESET failed...not retrying\n");
			break; // remove to retry
        } else {
            printk(KERN_INFO "PCOM_MSM_HSUSB_PHY_RESET DONE\n");
            break;
        }
    }
}
EXPORT_SYMBOL(pcom_usb_reset_phy);

void pcom_enable_hsusb_clk(void)
{
    pcom_clock_enable(PCOM_USB_HS_CLK);
}
EXPORT_SYMBOL(pcom_enable_hsusb_clk);

void pcom_disable_hsusb_clk(void)
{
    pcom_clock_disable(PCOM_USB_HS_CLK);
}
EXPORT_SYMBOL(pcom_disable_hsusb_clk);

// ---- SD card related proc_comm clients
void pcom_set_sdcard_clk_flags(int instance, int flags)
{
	switch(instance){
		case 1:
			pcom_set_clock_flags(PCOM_SDC1_CLK, flags);
			break;
		case 2:
			pcom_set_clock_flags(PCOM_SDC2_CLK, flags);
			break;
		case 3:
			pcom_set_clock_flags(PCOM_SDC3_CLK, flags);
			break;
		case 4:
			pcom_set_clock_flags(PCOM_SDC4_CLK, flags);
			break;
	}
}
EXPORT_SYMBOL(pcom_set_sdcard_clk_flags);

void pcom_set_sdcard_clk(int instance, int rate)
{
	switch(instance){
		case 1:
			pcom_clock_set_rate(PCOM_SDC1_CLK, rate);
			break;
		case 2:
			pcom_clock_set_rate(PCOM_SDC2_CLK, rate);
			break;
		case 3:
			pcom_clock_set_rate(PCOM_SDC3_CLK, rate);
			break;
		case 4:
			pcom_clock_set_rate(PCOM_SDC4_CLK, rate);
			break;
	}
}
EXPORT_SYMBOL(pcom_set_sdcard_clk);

uint32_t pcom_get_sdcard_clk(int instance)
{
	uint32_t rate = 0;
	switch(instance){
		case 1:
			rate = pcom_clock_get_rate(PCOM_SDC1_CLK);
			break;
		case 2:
			rate = pcom_clock_get_rate(PCOM_SDC2_CLK);
			break;
		case 3:
			rate = pcom_clock_get_rate(PCOM_SDC3_CLK);
			break;
		case 4:
			rate = pcom_clock_get_rate(PCOM_SDC4_CLK);
			break;
	}
	return rate;
}
EXPORT_SYMBOL(pcom_get_sdcard_clk);

void pcom_enable_sdcard_clk(int instance)
{
	switch(instance){
		case 1:
			pcom_clock_enable(PCOM_SDC1_CLK);
			break;
		case 2:
			pcom_clock_enable(PCOM_SDC2_CLK);
			break;
		case 3:
			pcom_clock_enable(PCOM_SDC3_CLK);
			break;
		case 4:
			pcom_clock_enable(PCOM_SDC4_CLK);
			break;
	}
}
EXPORT_SYMBOL(pcom_enable_sdcard_clk);

void pcom_disable_sdcard_clk(int instance)
{
	switch(instance){
		case 1:
			pcom_clock_disable(PCOM_SDC1_CLK);
			break;
		case 2:
			pcom_clock_disable(PCOM_SDC2_CLK);
			break;
		case 3:
			pcom_clock_disable(PCOM_SDC3_CLK);
			break;
		case 4:
			pcom_clock_disable(PCOM_SDC4_CLK);
			break;
	}
}
EXPORT_SYMBOL(pcom_disable_sdcard_clk);

static int msm_proc_comm_probe(struct platform_device *pdev)
{
    struct resource *res;

    proc_comm_data = devm_kzalloc(&pdev->dev, sizeof(*proc_comm_data), GFP_KERNEL);
    if (!proc_comm_data)
        return -ENOMEM;

    res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "shared-ram-base");
    if (!res) {
        dev_err(&pdev->dev, "Failed to get shared RAM base resource\n");
        return -EINVAL;
    }

    proc_comm_data->shared_ram_base = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(proc_comm_data->shared_ram_base))
        return PTR_ERR(proc_comm_data->shared_ram_base);

    res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "csr-base");
    if (!res) {
        dev_err(&pdev->dev, "Failed to get CSR base resource\n");
        return -EINVAL;
    }

    proc_comm_data->csr_base = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(proc_comm_data->csr_base))
        return PTR_ERR(proc_comm_data->csr_base);

    spin_lock_init(&proc_comm_lock);

    dev_info(&pdev->dev, "MSM proc_comm driver initialized\n");
    return 0;
}

static const struct of_device_id msm_proc_comm_dt_match[] = {
    { .compatible = "qcom,msm-proc-comm" },
    {},
};
MODULE_DEVICE_TABLE(of, msm_proc_comm_dt_match);

static struct platform_driver msm_proc_comm_driver = {
    .probe = msm_proc_comm_probe,
    .driver = {
        .name = "msm_proc_comm",
        .of_match_table = msm_proc_comm_dt_match,
    },
};

module_platform_driver(msm_proc_comm_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("J0SH1X <aljoshua.hell@gmail.com");
MODULE_DESCRIPTION("MSM Proc Comm Driver");
