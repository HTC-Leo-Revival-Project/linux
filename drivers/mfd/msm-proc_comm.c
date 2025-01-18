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
