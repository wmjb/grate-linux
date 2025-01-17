// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R-Mobile Reset Driver
 *
 * Copyright (C) 2014 Glider bvba
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/reboot.h>

/* SYSC Register Bank 2 */
#define RESCNT2		0x20		/* Reset Control Register 2 */

/* Reset Control Register 2 */
#define RESCNT2_PRES	0x80000000	/* Soft power-on reset */

static void rmobile_reset_handler(struct restart_data *data)
{
	pr_debug("%s %u\n", __func__, data->mode);

	/* Let's assume we have acquired the HPB semaphore */
	writel(RESCNT2_PRES, data->cb_data);
}

static int rmobile_reset_probe(struct platform_device *pdev)
{
	void __iomem *sysc_base2;
	int error;

	sysc_base2 = devm_platform_ioremap_resource(pdev, 1);
	if (!sysc_base2)
		return -ENODEV;

	error = devm_register_prioritized_restart_handler(&pdev->dev,
							  RESTART_PRIO_HIGH,
							  rmobile_reset_handler,
							  sysc_base2 + RESCNT2);
	if (error) {
		dev_err(&pdev->dev,
			"cannot register restart handler (err=%d)\n", error);
		return error;
	}

	return 0;
}

static const struct of_device_id rmobile_reset_of_match[] = {
	{ .compatible = "renesas,sysc-rmobile", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rmobile_reset_of_match);

static struct platform_driver rmobile_reset_driver = {
	.probe = rmobile_reset_probe,
	.driver = {
		.name = "rmobile_reset",
		.of_match_table = rmobile_reset_of_match,
	},
};

module_platform_driver(rmobile_reset_driver);

MODULE_DESCRIPTION("Renesas R-Mobile Reset Driver");
MODULE_AUTHOR("Geert Uytterhoeven <geert+renesas@glider.be>");
MODULE_LICENSE("GPL v2");
