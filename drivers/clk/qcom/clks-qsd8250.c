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

struct qsd8250_clk {
    struct clk_hw hw;
    unsigned int id;  /* PCOM clock ID */
};

static const unsigned long uart1_clk_rates[] = { 1843200UL, 0 };

#define to_qsd8250_clk(_hw) container_of(_hw, struct qsd8250_clk, hw)

/* clk_ops */
static int qsd8250_clk_enable(struct clk_hw *hw)
{
    struct qsd8250_clk *c = to_qsd8250_clk(hw);
    if (c->id == PCOM_EBI1_CLK || c->id == PCOM_EBI1_FIXED_CLK) { // EBI CLOCKS ARE ALWAYS ON, DO NOT ENABLE
        pr_info("Clock ID %u is EBI or EBI_FIXED, skipping enable\n", c->id);
        return 0;
    }
    int ret = pcom_clock_enable( c->id);
    if (ret < 0)
        pr_err("Failed to enable clock ID %u", c->id);
    else
        pr_info("Enabled clock ID %u\n", c->id);
    return ret;
}

static void qsd8250_clk_disable(struct clk_hw *hw)
{
    struct qsd8250_clk *c = to_qsd8250_clk(hw);
    if (c->id == PCOM_EBI1_CLK || c->id == PCOM_EBI1_FIXED_CLK) { // EBI CLOCKS ARE ALWAYS ON, DO NOT DISABLE
        pr_info("Clock ID %u is EBI or EBI_FIXED, skipping disable\n", c->id);
        return;
    }
    int ret = pcom_clock_disable(c->id);
    if (ret < 0)
        pr_err("Failed to disable clock ID %u", c->id);
    else
        pr_info("Disabled clock ID %u\n", c->id);
}

static int qsd8250_clk_get_rate(struct clk_hw *hw,struct clk_rate_request *req)
{
    struct qsd8250_clk *c = to_qsd8250_clk(hw);
    int rate = pcom_clock_get_rate(c->id);
    pr_info("Clock ID %u rate is %d\n", c->id, rate);
    return (rate > 0) ? rate : 0;
}

static int qsd8250_clk_set_rate(struct clk_hw *hw, unsigned long rate,
                                unsigned long parent_rate)
{
    struct qsd8250_clk *c = to_qsd8250_clk(hw);
    int id = c->id;
    return pcom_clock_set_rate(id, rate);
}

long qsd8250_clk_round_rate(struct clk_hw *hw, unsigned long rate, unsigned long *p_rate)
{
    unsigned long best = uart1_clk_rates[0];
    unsigned long diff, best_diff = ~0UL;
    int i;

    for (i = 0; i < ARRAY_SIZE(uart1_clk_rates); i++) {
        diff = (rate > uart1_clk_rates[i]) ? rate - uart1_clk_rates[i] : uart1_clk_rates[i] - rate;
        if (diff < best_diff) {
            best_diff = diff;
            best = uart1_clk_rates[i];
        }
    }

    return best;
}

static unsigned long qsd8250_clk_recalc_rate(struct clk_hw *hw, unsigned long p_rate)
{

	return p_rate;
}

static const struct clk_ops qsd8250_clk_ops = {
    .enable = qsd8250_clk_enable,
    .disable = qsd8250_clk_disable,
    .set_rate = qsd8250_clk_set_rate,
    .round_rate = qsd8250_clk_round_rate,
    .recalc_rate = qsd8250_clk_recalc_rate,
    .determine_rate = qsd8250_clk_get_rate,
};

static int qsd8250_clk_probe(struct platform_device *pdev)
{
    struct device_node *np = pdev->dev.of_node;
    struct clk_hw_onecell_data *clk_data;
    struct qsd8250_clk *clk;
    int nclks, i;

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
        pr_info("Registering clock ID %u\n", id);

        int err = clk_hw_register(NULL, &clk->hw);
        if (err < 0) {
            pr_err("Failed to register clock ID %u", id);
            return err;
        }
        clk_data->hws[i] = &clk->hw;

        // pr_info("clk %u init=%p ops=%p\n",
        //         id, clk->hw.init, clk->hw.init->ops);
    }

    clk_data->num = nclks;

    /* Register provider */
    int ret = of_clk_add_hw_provider(np, of_clk_hw_onecell_get, clk_data);
    if (ret)
        pr_err("clk provider registration failed\n");
    else
        pr_info("clk provider registered (%d clocks)\n", clk_data->num);

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