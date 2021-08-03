// SPDX-License-Identifier: GPL-2.0+
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>

struct srt_battery_device {
	struct i2c_client *client;
	struct device *dev;
	struct power_supply *battery;
	struct gpio_desc *powerdown_gpio;
};

static enum power_supply_property srt_battery_power_supply_props[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,

	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,

	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,

	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TECHNOLOGY,
};

static int readString(struct i2c_client *client_adap, char *buf, u8 address, u8 size)
{
	struct i2c_msg msg[2];

	msg[0].addr = client_adap->addr;
	msg[0].flags = 0;
	msg[0].buf = &address;
	msg[0].len = 1;

	msg[1].addr = client_adap->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = size;

	return i2c_transfer(client_adap->adapter, msg, 2);
}

static int srt_battery_power_supply_get_property(struct power_supply *psy,
											enum power_supply_property psp,
											union power_supply_propval *val)
{
	struct srt_battery_device *srt_battery = power_supply_get_drvdata(psy);
	struct i2c_client *client = srt_battery->client;

	static char strBuf[8];

	gpiod_set_value(srt_battery->powerdown_gpio, 0); // power up
	usleep_range(1000, 1500); // 1ms from ACPI wait till IC is ready

	switch (psp) {
	case POWER_SUPPLY_PROP_MANUFACTURER:
		readString(client, strBuf, 0x46, 8);
		val->strval = strBuf;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		readString(client, strBuf, 0x52, 8);
		val->strval = strBuf;
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		sprintf(strBuf, "%04x", i2c_smbus_read_word_data(client, 0x44));
		strBuf[4] = '\0';
		val->strval = strBuf;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = i2c_smbus_read_word_data(client, 0x28);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = i2c_smbus_read_word_data(client, 0x3C) * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = i2c_smbus_read_word_data(client, 0x2C) * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = i2c_smbus_read_word_data(client, 0x28)
					* i2c_smbus_read_word_data(client, 0x2C) * 10;
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = i2c_smbus_read_word_data(client, 0x3A);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (i2c_smbus_read_byte_data(client, 0x02) & 0x01)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		readString(client, strBuf, 0x5A, 4);
		if (strncmp(strBuf, "LION", 4) == 0)
			val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		else
			val->intval = POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = i2c_smbus_read_word_data(client, 0x3E) * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = i2c_smbus_read_word_data(client, 0x20) * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = (int16_t)i2c_smbus_read_word_data(client, 0x24) * 1000;
		break;
	default:
		return -EINVAL;
	}

	gpiod_set_value(srt_battery->powerdown_gpio, 1); // powerdown

	return 0;
}

static const struct power_supply_desc srt_battery_power_supply_desc = {
	.name = "surface-rt-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = srt_battery_power_supply_props,
	.num_properties = ARRAY_SIZE(srt_battery_power_supply_props),
	.get_property = srt_battery_power_supply_get_property,
};

static int srt_battery_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct srt_battery_device *srt_battery;
	struct power_supply_config psy_cfg = {};
	int ret = -1;

	srt_battery = devm_kzalloc(dev, sizeof(*srt_battery), GFP_KERNEL);
	if (!srt_battery)
		return -ENOMEM;

	srt_battery->client = client;
	srt_battery->dev = dev;

	srt_battery->powerdown_gpio = devm_gpiod_get(srt_battery->dev, "powerdown", GPIOD_OUT_LOW);
	if (IS_ERR(srt_battery->powerdown_gpio)) {
		ret = PTR_ERR(srt_battery->powerdown_gpio);
		dev_err(srt_battery->dev, "Failed to get powerdown");
		return ret;
	}

	psy_cfg.drv_data = srt_battery;

	srt_battery->battery = devm_power_supply_register(srt_battery->dev, &srt_battery_power_supply_desc, &psy_cfg);

	if (PTR_ERR_OR_ZERO(srt_battery->battery) < 0) {
		dev_err(srt_battery->dev, "Failed to register power supply\n");
		return ret;
	}

	i2c_set_clientdata(client, srt_battery);

	gpiod_set_value(srt_battery->powerdown_gpio, 1); // powerdown

	return 0;
}

static const struct i2c_device_id srt_battery_i2c_ids[] = {
	{ "surface-rt-battery", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, srt_battery_i2c_ids);

static const struct of_device_id srt_battery_of_match[] = {
	{ .compatible = "microsoft,surface-rt-battery", },
	{},
};
MODULE_DEVICE_TABLE(of, srt_battery_of_match);

static struct i2c_driver srt_battery_driver = {
	.driver = {
		.name = "surface-rt-battery",
		.of_match_table = of_match_ptr(srt_battery_of_match),
	},
	.probe = srt_battery_probe,
	.id_table = srt_battery_i2c_ids,
};
module_i2c_driver(srt_battery_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jonas Schwöbel <jonasschwoebel@yahoo.de>");
MODULE_DESCRIPTION("Surface RT Battery driver");
