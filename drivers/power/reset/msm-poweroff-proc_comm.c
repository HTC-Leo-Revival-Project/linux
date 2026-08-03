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


static struct platform_device *pdev_global = NULL;

static int do_msm_poweroff(struct sys_off_data *data)
{
    msm_proc_comm(PCOM_POWER_DOWN, 0, 0);


    for (;;);

    return NOTIFY_DONE;
}

static int do_msm_restart(struct sys_off_data *data)
{
    msm_proc_comm(PCOM_RESET_CHIP, 0, 0);

    for (;;);

    return NOTIFY_DONE;
}

static int msm_restart_probe(struct platform_device *pdev)
{
    pdev_global = pdev;

    devm_register_sys_off_handler(&pdev->dev, SYS_OFF_MODE_RESTART,
                                  128, do_msm_restart, NULL);

    devm_register_sys_off_handler(&pdev->dev, SYS_OFF_MODE_POWER_OFF,
                                  SYS_OFF_PRIO_DEFAULT, do_msm_poweroff,
                                  NULL);

    return 0;
}

static const struct of_device_id of_msm_restart_match[] = {
    { .compatible = "qcom,pshold-proc_comm", },
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
MODULE_AUTHOR("j0sh1x <aljoshua.hell@gmail.com>");
MODULE_DESCRIPTION("MSM Restart Proc_Comm Driver");
