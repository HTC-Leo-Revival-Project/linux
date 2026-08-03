/*
 * Minimal Qualcomm PCOM -> clk interface for QSD8250
 *
 * On QSD8250, clocks are enabled via pcom_enable_clk(id).
 * This exposes a basic clk API that other drivers can use.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/mach-msm/msm-proc_comm.h>
#include <linux/mach-msm/pcom_clocks.h>

const struct device *pcom_clk_dev;

struct qsd8250_clk {
    struct clk_hw hw;
    unsigned int id;  /* PCOM clock ID */
    unsigned long rate; /* Cached rate for this clock */
};

static const unsigned long uart1_clk_rates[] = { 1843200, 0 };

#define to_qsd8250_clk(_hw) container_of(_hw, struct qsd8250_clk, hw)

/* clk_ops */
static int qsd8250_clk_enable(struct clk_hw *hw)
{
    struct qsd8250_clk *c = to_qsd8250_clk(hw);
    if (c->id == PCOM_EBI1_CLK || c->id == PCOM_EBI1_FIXED_CLK) { // EBI CLOCKS ARE ALWAYS ON, DO NOT ENABLE
        dev_info(pcom_clk_dev, "Clock ID %u is EBI or EBI_FIXED, skipping enable\n", c->id);
        return -1;
    }
    int ret = pcom_clock_enable(c->id);
    if (ret < 0)
        dev_err(pcom_clk_dev, "Failed to enable clock ID %u", c->id);
    else if (c-> id != 21 && c->id != 22 && c->id != 9) // Dont log sdcard and i2c clocks they are noisy
        dev_info(pcom_clk_dev, "Enabled clock ID %u\n", c->id);
    return ret;
}

static void qsd8250_clk_disable(struct clk_hw *hw)
{
    struct qsd8250_clk *c = to_qsd8250_clk(hw);
    if (c->id == PCOM_EBI1_CLK || c->id == PCOM_EBI1_FIXED_CLK || c->id == 19 || c->id == 21) { // EBI CLOCKS ARE ALWAYS ON, DO NOT DISABLE
        dev_info(pcom_clk_dev, "Clock ID %u is EBI or EBI_FIXED or SDC1, skipping disable\n", c->id);
        return;
    }
    int ret = pcom_clock_disable(c->id);
    if (ret < 0)
        dev_err(pcom_clk_dev, "Failed to disable clock ID %u", c->id);
    else if (c-> id != 21 && c->id != 22 && c->id != 9) // Dont log sdcard and i2c clocks they are noisy
        dev_info(pcom_clk_dev, "Disabled clock ID %u\n", c->id);
}

static int qsd8250_clk_determine_rate(struct clk_hw *hw,struct clk_rate_request *req)
{
    /*
	 * PCOM handles rate rounding and we don't have a way to
	 * know what the rate will be, so just return whatever
	 * rate is requested.
	 */
	return 0;
}

static int qsd8250_clk_set_rate(struct clk_hw *hw, unsigned long rate,
                                unsigned long parent_rate)
{
    struct qsd8250_clk *c = to_qsd8250_clk(hw);
    int id = c->id;
    c->rate = rate;
    return pcom_clock_set_rate(id, (uint)rate);
}

static unsigned long qsd8250_clk_recalc_rate(struct clk_hw *hw,
                                             unsigned long parent_rate)
{
    struct qsd8250_clk *c = to_qsd8250_clk(hw);
    return c->rate;
}

static int qsd8250_clk_is_enabled(struct clk_hw *hw)
{
    struct qsd8250_clk *c = to_qsd8250_clk(hw);
    return pcom_clock_is_enabled(c->id);
}

static const struct clk_ops qsd8250_clk_ops = {
    .enable = qsd8250_clk_enable,
    .disable = qsd8250_clk_disable,
    .set_rate = qsd8250_clk_set_rate,
    .recalc_rate = qsd8250_clk_recalc_rate,
    .determine_rate = qsd8250_clk_determine_rate,
    .is_enabled = qsd8250_clk_is_enabled,
};

static int qsd8250_clk_probe(struct platform_device *pdev)
{
    struct device_node *np = pdev->dev.of_node;
    struct clk_hw_onecell_data *clk_data;
    struct qsd8250_clk *clk;
    int nclks, i;

    pcom_clk_dev = &pdev->dev;

    /* Count clocks from DT */
    nclks = of_property_count_u32_elems(np, "qcom,clk-ids");
    if (nclks <= 0)
        return -EINVAL;

    /* Allocate onecell data */
    clk_data = devm_kzalloc(&pdev->dev,
                            struct_size(clk_data, hws, nclks),
                            GFP_KERNEL);
    if (!clk_data)
        return -ENOMEM;

    if (!is_pcom_probed()) {
        return -EPROBE_DEFER;
    }

    for (i = 0; i < nclks; i++) {
        u32 id;
        struct clk_init_data *init;
        char *name;

        /* Read clock ID from DT */
        of_property_read_u32_index(np, "qcom,clk-ids", i, &id);

        /* Allocate persistent qsd8250_clk structure */
        clk = devm_kzalloc(&pdev->dev, sizeof(*clk), GFP_KERNEL);
        if (!clk)
            return -ENOMEM;

        clk->id = id;

        /* Allocate persistent init structure */
        init = devm_kzalloc(&pdev->dev, sizeof(*init), GFP_KERNEL);
        if (!init)
            return -ENOMEM;

        /* Allocate persistent name string */
        name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "clk.%u", id);
        if (!name)
            return -ENOMEM;

        init->name = name;
        init->ops = &qsd8250_clk_ops;
        init->flags = 0;
        init->num_parents = 0;

        clk->hw.init = init;
        dev_info(pcom_clk_dev, "Registering clock ID %u\n", id);

        int err = clk_hw_register(NULL, &clk->hw);
        if (err < 0) {
            dev_err(pcom_clk_dev, "Failed to register clock ID %u", id);
            return err;
        }
        clk_data->hws[i] = &clk->hw;

    }

    clk_data->num = nclks;

    /* Register provider */
    int ret = of_clk_add_hw_provider(np, of_clk_hw_onecell_get, clk_data);
    if (ret)
        dev_err(pcom_clk_dev, "clk provider registration failed\n");
    else
        dev_info(pcom_clk_dev, "clk provider registered (%d clocks)\n", clk_data->num);

    

    return 0;
}

static const struct of_device_id qsd8250_clk_of_match[] = {
    { .compatible = "qcom,qsd8250-clk", },
    { }
};
MODULE_DEVICE_TABLE(of, qsd8250_clk_of_match);

static struct platform_driver qsd8250_clk_driver = {
    .probe = qsd8250_clk_probe,
    .driver = {
        .name = "qsd8250-pcom-clk",
        .of_match_table = qsd8250_clk_of_match,
    },
};
module_platform_driver(qsd8250_clk_driver);

MODULE_DESCRIPTION("Minimal QSD8250 PCOM clock wrapper");
MODULE_LICENSE("GPL");