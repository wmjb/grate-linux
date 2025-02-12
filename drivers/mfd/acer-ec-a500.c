// SPDX-License-Identifier: GPL-2.0+
/*
 * Acer Iconia Tab A500 Embedded Controller Driver
 *
 * Copyright 2020 GRATE-driver project
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>

#define A500_EC_I2C_ERR_TIMEOUT		500
#define A500_EC_POWER_CMD_TIMEOUT	1000

/*
 * Controller's firmware expects specific command opcodes to be used for the
 * corresponding registers. Unsupported commands are skipped by the firmware.
 */
#define CMD_SHUTDOWN			0x0
#define CMD_WARM_REBOOT			0x0
#define CMD_COLD_REBOOT			0x1

enum {
	REG_CURRENT_NOW = 0x03,
	REG_SHUTDOWN = 0x52,
	REG_WARM_REBOOT = 0x54,
	REG_COLD_REBOOT = 0x55,
};

static int a500_ec_read(void *context, const void *reg_buf, size_t reg_size,
			void *val_buf, size_t val_sizel)
{
	struct i2c_client *client = context;
	unsigned int reg, retries = 5;
	u16 *ret_val = val_buf;
	s32 ret = 0;

	reg = *(u8 *)reg_buf;

	while (retries-- > 0) {
		ret = i2c_smbus_read_word_data(client, reg);
		if (ret >= 0)
			break;

		msleep(A500_EC_I2C_ERR_TIMEOUT);
	}

	if (ret < 0) {
		dev_err(&client->dev, "read 0x%x failed: %d\n", reg, ret);
		return ret;
	}

	*ret_val = ret;

	if (reg == REG_CURRENT_NOW)
		fsleep(10000);

	return 0;
}

static int a500_ec_write(void *context, const void *data, size_t count)
{
	struct i2c_client *client = context;
	unsigned int reg, val, retries = 5;
	s32 ret = 0;

	reg = *(u8  *)(data + 0);
	val = *(u16 *)(data + 1);

	while (retries-- > 0) {
		ret = i2c_smbus_write_word_data(client, reg, val);
		if (ret >= 0)
			break;

		msleep(A500_EC_I2C_ERR_TIMEOUT);
	}

	if (ret < 0) {
		dev_err(&client->dev, "write 0x%x failed: %d\n", reg, ret);
		return ret;
	}

	return 0;
}

static const struct regmap_config a500_ec_regmap_config = {
	.name = "KB930",
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = 0xff,
};

static const struct regmap_bus a500_ec_regmap_bus = {
	.reg_format_endian_default = REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default = REGMAP_ENDIAN_LITTLE,
	.write = a500_ec_write,
	.read = a500_ec_read,
	.max_raw_read = 2,
};

static void a500_ec_power_off_handler(struct power_off_data *data)
{
	struct i2c_client *client = data->cb_data;

	i2c_smbus_write_word_data(client, REG_SHUTDOWN, CMD_SHUTDOWN);

	mdelay(A500_EC_POWER_CMD_TIMEOUT);
}

static void a500_ec_restart_handler(struct restart_data *data)
{
	struct i2c_client *client = data->cb_data;

	if (data->mode == REBOOT_WARM)
		i2c_smbus_write_word_data(client,
					  REG_WARM_REBOOT, CMD_WARM_REBOOT);
	else
		i2c_smbus_write_word_data(client,
					  REG_COLD_REBOOT, CMD_COLD_REBOOT);

	mdelay(A500_EC_POWER_CMD_TIMEOUT);
}

static const struct mfd_cell a500_ec_cells[] = {
	{ .name = "acer-a500-iconia-battery", },
	{ .name = "acer-a500-iconia-leds", },
};

static int a500_ec_probe(struct i2c_client *client)
{
	struct sys_off_handler *sys_off;
	struct regmap *regmap;
	int err;

	regmap = devm_regmap_init(&client->dev, &a500_ec_regmap_bus,
				  client, &a500_ec_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	err = devm_mfd_add_devices(&client->dev, PLATFORM_DEVID_AUTO,
				   a500_ec_cells, ARRAY_SIZE(a500_ec_cells),
				   NULL, 0, NULL);
	if (err) {
		dev_err(&client->dev, "failed to add sub-devices: %d\n", err);
		return err;
	}

	if (of_device_is_system_power_controller(client->dev.of_node)) {
		sys_off = devm_kzalloc(&client->dev, sizeof(*sys_off),
				       GFP_KERNEL);
		if (!sys_off)
			return -ENOMEM;

		sys_off->cb_data = client;
		sys_off->restart_cb = a500_ec_restart_handler;
		sys_off->restart_priority = RESTART_PRIO_HIGH;
		sys_off->power_off_cb = a500_ec_power_off_handler;
		sys_off->power_off_priority = POWEROFF_PRIO_HIGH;

		err = devm_register_sys_off_handler(&client->dev, sys_off);
		if (err)
			return err;
	}

	return 0;
}

static const struct of_device_id a500_ec_match[] = {
	{ .compatible = "acer,a500-iconia-ec" },
	{ }
};
MODULE_DEVICE_TABLE(of, a500_ec_match);

static struct i2c_driver a500_ec_driver = {
	.driver = {
		.name = "acer-a500-embedded-controller",
		.of_match_table = a500_ec_match,
	},
	.probe_new = a500_ec_probe,
};
module_i2c_driver(a500_ec_driver);

MODULE_DESCRIPTION("Acer Iconia Tab A500 Embedded Controller driver");
MODULE_AUTHOR("Dmitry Osipenko <digetx@gmail.com>");
MODULE_LICENSE("GPL");
