// SPDX-License-Identifier: GPL-2.0-only
/*
 * HTC QSD8250 GPIO Matrix Keypad
 *
 * Based on HTC Leo UEFI keypad implementation.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <linux/gpio/consumer.h>
#include <linux/input.h>

#include <linux/workqueue.h>
#include <linux/jiffies.h>


struct qsd8250_key {
	unsigned int row;
	unsigned int col;
	unsigned int code;
	bool active_low;
	bool pressed;
};


struct qsd8250_matrix {

	struct device *dev;
	struct gpio_desc **rows;
	struct gpio_desc **cols;
	unsigned int nrows;
	unsigned int ncols;
	struct qsd8250_key *keys;
	unsigned int nkeys;
	struct input_dev *input;
	struct delayed_work scan_work;
	unsigned int poll_interval;
};



static bool qsd8250_matrix_read_key(struct qsd8250_matrix *matrix,struct qsd8250_key *key) {
	int value;

	gpiod_set_value_cansleep(matrix->rows[key->row],0);

	value = gpiod_get_value_cansleep(matrix->cols[key->col]);

	gpiod_set_value_cansleep(matrix->rows[key->row],1);

	if (value < 0)
		return false;

	return (!!value) ^ key->active_low;
}



static void qsd8250_matrix_scan(struct work_struct *work) {
	struct qsd8250_matrix *matrix;
	unsigned int i;

	matrix = container_of(to_delayed_work(work),struct qsd8250_matrix,scan_work);

	for (i = 0; i < matrix->nkeys; i++) {
		struct qsd8250_key *key = &matrix->keys[i];
		bool pressed = qsd8250_matrix_read_key(matrix,key);

		if (pressed != key->pressed) {
			input_report_key(matrix->input,key->code,pressed);
			input_sync(matrix->input);
			key->pressed = pressed;
		}
	}
    schedule_delayed_work(&matrix->scan_work,msecs_to_jiffies(matrix->poll_interval));
}



static int qsd8250_matrix_parse_keys(struct device *dev,struct qsd8250_matrix *matrix) {
	struct device_node *keymap;
	struct device_node *child;

	unsigned int count;
	unsigned int index = 0;

	keymap = of_get_child_by_name(dev->of_node,"keymap");

	if (!keymap)
		return -EINVAL;

	count =of_get_child_count(keymap);

	if (!count) {
        of_node_put(keymap);
        return -EINVAL;
	}

	matrix->keys = devm_kcalloc(dev,count,sizeof(*matrix->keys),GFP_KERNEL);

	if (!matrix->keys) {
		of_node_put(keymap);
		return -ENOMEM;
	}

	matrix->nkeys = count;

	for_each_child_of_node(keymap,child) {
		u32 row;
		u32 col;
		u32 code;

		if (of_property_read_u32(child,"row",&row))
			continue;

		if (of_property_read_u32(child,"col",&col))
		    continue;

		if (of_property_read_u32(child,"linux,code",&code))
			continue;

		matrix->keys[index].row = row;
		matrix->keys[index].col = col;

		matrix->keys[index].code = code;

		matrix->keys[index].active_low = of_property_read_bool(child,"active-low");

		matrix->keys[index].pressed = false;

		index++;
	}

	of_node_put(keymap);

	matrix->nkeys = index;

	return 0;
}



static int qsd8250_matrix_probe(struct platform_device *pdev) {
	struct device *dev = &pdev->dev;
	struct qsd8250_matrix *matrix;
	int i;
	int ret;

	matrix = devm_kzalloc(dev,sizeof(*matrix),GFP_KERNEL);

	if (!matrix)
		return -ENOMEM;

	matrix->dev = dev;

	matrix->nrows = gpiod_count(dev,"row");

	matrix->ncols = gpiod_count(dev,"col");

	if (matrix->nrows <= 0 || matrix->ncols <= 0) {
		dev_err(dev,"missing row/col GPIOs\n");
		return -EINVAL;
	}

	matrix->rows = devm_kcalloc(dev,matrix->nrows,sizeof(*matrix->rows),GFP_KERNEL);

	matrix->cols = devm_kcalloc(dev,matrix->ncols,sizeof(*matrix->cols),GFP_KERNEL);

	if (!matrix->rows ||
	    !matrix->cols)
		return -ENOMEM;

	for (i = 0; i < matrix->nrows; i++) {
		matrix->rows[i] = devm_gpiod_get_index(dev,"row",i,GPIOD_OUT_HIGH);

		if (IS_ERR(matrix->rows[i]))
			return PTR_ERR(matrix->rows[i]);
	}

	for (i = 0; i < matrix->ncols; i++) {

		matrix->cols[i] = devm_gpiod_get_index(dev,"col",i,GPIOD_IN);

		if (IS_ERR(matrix->cols[i]))
			return PTR_ERR(matrix->cols[i]);
	}

	ret =qsd8250_matrix_parse_keys(dev,matrix);

	if (ret)
		return ret;

	matrix->input = devm_input_allocate_device(dev);

	if (!matrix->input)
		return -ENOMEM;



	matrix->input->name = "HTC QSD8250 Matrix Keypad";
	matrix->input->phys = "qsd8250-matrix/input0";

    __set_bit(EV_KEY,matrix->input->evbit);

	for (i = 0; i < matrix->nkeys; i++) {
		__set_bit(matrix->keys[i].code,matrix->input->keybit);
	}

	ret = input_register_device(matrix->input);

	if (ret)
		return ret;

	matrix->poll_interval = 20;

	INIT_DELAYED_WORK(&matrix->scan_work,qsd8250_matrix_scan);

	schedule_delayed_work(&matrix->scan_work,msecs_to_jiffies(matrix->poll_interval));

	platform_set_drvdata(pdev,matrix);

	dev_info(dev,"HTC QSD8250 matrix keypad: %u keys\n",matrix->nkeys);
	return 0;
}



static void qsd8250_matrix_remove(struct platform_device *pdev) {
	struct qsd8250_matrix *matrix = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&matrix->scan_work);
}

static const struct of_device_id qsd8250_matrix_of_match[] = {
	{
		.compatible = "htc,qsd8250-matrix-keys",
	},
	{}
};


MODULE_DEVICE_TABLE(of,qsd8250_matrix_of_match);

static struct platform_driver qsd8250_matrix_driver = {

	.driver = {
		.name ="qsd8250-keymatrix",
		.of_match_table =qsd8250_matrix_of_match,
	},
	.probe = qsd8250_matrix_probe,
	.remove = qsd8250_matrix_remove,
};

module_platform_driver(qsd8250_matrix_driver);

MODULE_AUTHOR("HTC Leo Revival Project");
MODULE_DESCRIPTION("HTC QSD8250 GPIO matrix keypad");
MODULE_LICENSE("GPL");