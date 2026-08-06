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
#include <linux/of_address.h>
#include <linux/slab.h>
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
    int ret = -1;

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

int pcom_clock_enable(unsigned id)
{
	return msm_proc_comm(PCOM_CLK_REGIME_SEC_ENABLE, &id, 0);
}
EXPORT_SYMBOL(pcom_clock_enable);

int pcom_clock_disable(unsigned id)
{
	return msm_proc_comm(PCOM_CLK_REGIME_SEC_DISABLE, &id, 0);
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

void pcom_usb_vbus_power(int state)
{
    unsigned v = PCOM_MPP_FOR_USB_VBUS;
	unsigned s = (PM_MPP__DLOGIC__LVL_VDD << 16) | (state ? PM_MPP__DLOGIC_OUT__CTRL_HIGH : PM_MPP__DLOGIC_OUT__CTRL_LOW);

	msm_proc_comm(PCOM_PM_MPP_CONFIG, &v, &s);

    if(PCOM_CMD_SUCCESS != readl((void __iomem *)APP_STATUS)) {
        printk(KERN_INFO "Error: PCOM_MPP_CONFIG failed... not retrying\n");
    } else {
        printk(KERN_INFO "PCOM_MPP_CONFIG DONE\n");
    }
}
EXPORT_SYMBOL(pcom_usb_vbus_power);

bool is_pcom_probed(void) {
	if (proc_comm_data && proc_comm_data->shared_ram_base && proc_comm_data->csr_base) {
		return true;
	} else {
		return false;
	}
}
EXPORT_SYMBOL(is_pcom_probed);

static const struct of_device_id msm_proc_comm_dt_match[] = {
    { .compatible = "qcom,msm-proc-comm" },
    {},
};
MODULE_DEVICE_TABLE(of, msm_proc_comm_dt_match);

static int __init msm_proc_comm_early_init(void)
{
    struct device_node *np;
    struct resource res;

    np = of_find_compatible_node(NULL, NULL, "qcom,msm-proc-comm");
    if (!np) {
        pr_err("MSM proc_comm DT node not found\n");
        return -ENODEV;
    }

    proc_comm_data = kzalloc(sizeof(*proc_comm_data), GFP_KERNEL);
    if (!proc_comm_data)
        return -ENOMEM;

    if (of_address_to_resource(np, 0, &res)) {
        pr_err("Failed to get shared RAM resource\n");
        return -EINVAL;
    }
    proc_comm_data->shared_ram_base = ioremap(res.start, resource_size(&res));
    if (!proc_comm_data->shared_ram_base)
        return -ENOMEM;

    if (of_address_to_resource(np, 1, &res)) {
        pr_err("Failed to get CSR resource\n");
        return -EINVAL;
    }
    proc_comm_data->csr_base = ioremap(res.start, resource_size(&res));
    if (!proc_comm_data->csr_base)
        return -ENOMEM;

    spin_lock_init(&proc_comm_lock);
    pr_info("MSM proc_comm initialized early\n");

    return 0;
}

early_initcall(msm_proc_comm_early_init);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("J0SH1X <aljoshua.hell@gmail.com");
MODULE_DESCRIPTION("MSM Proc Comm Driver");
