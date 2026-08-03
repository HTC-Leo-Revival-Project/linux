#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/clk.h>

static int hack_clk_probe(struct platform_device *pdev)
{
    struct clk *clk;
    int ret;

    pr_info("hack-clk: probing\n");

    clk = devm_clk_get(&pdev->dev, NULL);
if (IS_ERR(clk)) {
    pr_err("clk get failed: %ld\n", PTR_ERR(clk));
    return PTR_ERR(clk);
}

    ret = clk_prepare_enable(clk);
    if (ret) {
        pr_err("hack-clk: failed to enable clock\n");
        return ret;
    }

    pr_info("hack-clk: clock enabled via DT\n");
    return 0;
}

static void hack_clk_remove(struct platform_device *pdev)
{
    struct clk *clk;

    clk = devm_clk_get(&pdev->dev, "core");
    if (!IS_ERR(clk))
        clk_disable_unprepare(clk);

    return;
}

static const struct of_device_id hack_clk_of_match[] = {
    { .compatible = "qcom,hack-clk", },
    { }
};
MODULE_DEVICE_TABLE(of, hack_clk_of_match);

static struct platform_driver hack_clk_driver = {
    .probe = hack_clk_probe,
    .remove = hack_clk_remove,
    .driver = {
        .name = "hack-clk",
        .of_match_table = hack_clk_of_match,
    },
};

module_platform_driver(hack_clk_driver);

MODULE_LICENSE("GPL");