// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2013, The Linux Foundation. All rights reserved.
 * Copyright (c) 2025, HtcLeoRevivalProject.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/pm.h>
#include <linux/mach-msm/msm-proc_comm.h>

// Define pdev_global as a global pointer
static struct platform_device *pdev_global = NULL;

static int do_msm_poweroff(struct sys_off_data *data)
{
    dev_info(&pdev_global->dev, "MSM poweroff received shutdown request\n");
    msm_proc_comm(PCOM_POWER_DOWN, 0, 0);

    // Enter an infinite loop to simulate poweroff
    for (;;);

    return NOTIFY_DONE; // Technically unreachable
}

static int msm_restart_probe(struct platform_device *pdev)
{
    pdev_global = pdev; // Initialize the global variable

    devm_register_sys_off_handler(&pdev->dev, SYS_OFF_MODE_RESTART,
                                  128, do_msm_poweroff, NULL);

    devm_register_sys_off_handler(&pdev->dev, SYS_OFF_MODE_POWER_OFF,
                                  SYS_OFF_PRIO_DEFAULT, do_msm_poweroff,
                                  NULL);

    dev_info(&pdev->dev, "MSM poweroff proc_comm driver initialized\n");
    return 0;
}

static const struct of_device_id of_msm_restart_match[] = {
    { .compatible = "qcom,pshold", },
    {},
};
MODULE_DEVICE_TABLE(of, of_msm_restart_match);

static struct platform_driver msm_restart_driver = {
    .probe = msm_restart_probe,
    .driver = {
        .name = "msm-restart",
        .of_match_table = of_match_ptr(of_msm_restart_match),
    },
};

builtin_platform_driver(msm_restart_driver);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("The Linux Foundation");
MODULE_DESCRIPTION("MSM Restart Proc_Comm Driver");
