// SPDX-License-Identifier: GPL-2.0-only
/*
 * ROHM Semiconductor GP7101 LED Driver
 *
 * Copyright (C) 2013 Ideas on board SPRL
 *
 * Contact: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/platform_data/gp7101.h>
#include <linux/slab.h>

#define GP7101_PSCNT1				0x03

struct gp7101 {
	struct i2c_client *client;
	struct backlight_device *backlight;
	struct gp7101_platform_data *pdata;
};

static int gp7101_write(struct gp7101 *bd, u8 reg, u8 data)
{
	return i2c_smbus_write_byte_data(bd->client, reg, data);
}

static int gp7101_backlight_update_status(struct backlight_device *backlight)
{
	struct gp7101 *bd = bl_get_data(backlight);
	int brightness = backlight_get_brightness(backlight);
	printk("brightness update =%d\n",brightness);
	if(brightness >=253)
		brightness = 255;
	else if(brightness <=2)
		brightness = 0;	
	gp7101_write(bd, GP7101_PSCNT1, brightness);

	return 0;
}

static int gp7101_backlight_check_fb(struct backlight_device *backlight,
				       struct fb_info *info)
{
	struct gp7101 *bd = bl_get_data(backlight);

	return bd->pdata->fbdev == NULL || bd->pdata->fbdev == info->dev;
}

static const struct backlight_ops gp7101_backlight_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= gp7101_backlight_update_status,
	.check_fb	= gp7101_backlight_check_fb,
};

static int gp7101_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct gp7101_platform_data *pdata;
	struct backlight_device *backlight;
	struct backlight_properties props;
	struct gp7101 *bd;
	u32 value;
	int ret;
	
	pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
	ret = of_property_read_u32(node, "default-brightness-level",&value);
	pdata->def_value = value;
	bd = devm_kzalloc(&client->dev, sizeof(*bd), GFP_KERNEL);
	if (!bd)
		return -ENOMEM;

	bd->client = client;
	bd->pdata = pdata;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = 256;
	props.brightness = clamp_t(unsigned int, pdata->def_value, 0,
				   props.max_brightness);

	backlight = devm_backlight_device_register(&client->dev,
					      dev_name(&client->dev),
					      &bd->client->dev, bd,
					      &gp7101_backlight_ops, &props);
	if (IS_ERR(backlight)) {
		dev_err(&client->dev, "failed to register backlight\n");
		return PTR_ERR(backlight);
	}

	backlight_update_status(backlight);
	i2c_set_clientdata(client, backlight);
	printk("gp7101_probe ok!!\n");
	return 0;
}

static int gp7101_remove(struct i2c_client *client)
{
	struct backlight_device *backlight = i2c_get_clientdata(client);

	backlight->props.brightness = 0;
	backlight_update_status(backlight);

	return 0;
}

static const struct i2c_device_id gp7101_ids[] = {
	{ "gp7101", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, gp7101_ids);

static struct i2c_driver gp7101_driver = {
	.driver = {
		.name = "gp7101",
	},
	.probe = gp7101_probe,
	.remove = gp7101_remove,
	.id_table = gp7101_ids,
};

module_i2c_driver(gp7101_driver);

MODULE_DESCRIPTION("Linearin GP7101 Backlight Driver");
MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_LICENSE("GPL");
