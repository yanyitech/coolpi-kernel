// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim MAX96745 GMSL2 Serializer with eDP1.4a/DP1.4 Input
 *
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_of.h>
#include <drm/drm_connector.h>
#include <drm/drm_probe_helper.h>

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/extcon-provider.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/max96745.h>

struct max96745_bridge {
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct drm_connector connector;
	struct drm_panel *panel;

	struct device *dev;
	struct regmap *regmap;
	struct {
		struct gpio_desc *gpio;
		int irq;
		bool irq_enabled;
		atomic_t triggered;
	} lock;
	struct extcon_dev *extcon;
};

#define to_max96745_bridge(x)	container_of(x, struct max96745_bridge, x)

static const unsigned int max96745_bridge_cable[] = {
	EXTCON_JACK_VIDEO_OUT,
	EXTCON_NONE,
};

static int max96745_bridge_connector_get_modes(struct drm_connector *connector)
{
	struct max96745_bridge *ser = to_max96745_bridge(connector);

	if (ser->next_bridge)
		return drm_bridge_get_modes(ser->next_bridge, connector);

	return drm_panel_get_modes(ser->panel, connector);
}

static const struct drm_connector_helper_funcs
max96745_bridge_connector_helper_funcs = {
	.get_modes = max96745_bridge_connector_get_modes,
};

static enum drm_connector_status
max96745_bridge_connector_detect(struct drm_connector *connector, bool force)
{
	struct max96745_bridge *ser = to_max96745_bridge(connector);
	struct drm_bridge *bridge = &ser->bridge;
	struct drm_bridge *prev_bridge = drm_bridge_get_prev_bridge(bridge);

	if (!drm_kms_helper_is_poll_worker())
		return connector->status;

	if (prev_bridge && (prev_bridge->ops & DRM_BRIDGE_OP_DETECT))
		return drm_bridge_detect(prev_bridge);

	return drm_bridge_detect(bridge);
}

static const struct drm_connector_funcs max96745_bridge_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = max96745_bridge_connector_detect,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static bool max96745_bridge_link_locked(struct max96745_bridge *ser)
{
	u32 val;

	if (regmap_read(ser->regmap, 0x002a, &val))
		return false;

	if (!FIELD_GET(LINK_LOCKED, val))
		return false;

	return true;
}

static bool max96745_bridge_vid_tx_active(struct max96745_bridge *ser)
{
	u32 val;

	if (regmap_read(ser->regmap, 0x0107, &val))
		return false;

	if (!FIELD_GET(VID_TX_ACTIVE_A | VID_TX_ACTIVE_B, val))
		return false;

	return true;
}

static int max96745_bridge_attach(struct drm_bridge *bridge,
				  enum drm_bridge_attach_flags flags)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);
	struct drm_connector *connector = &ser->connector;
	int ret;

	ret = drm_of_find_panel_or_bridge(bridge->of_node, 1, -1, &ser->panel,
					  &ser->next_bridge);
	if (ret)
		return ret;

	if (ser->next_bridge) {
		ret = drm_bridge_attach(bridge->encoder, ser->next_bridge,
					bridge, DRM_BRIDGE_ATTACH_NO_CONNECTOR);
		if (ret)
			return ret;
	}

	connector->polled = DRM_CONNECTOR_POLL_CONNECT |
			    DRM_CONNECTOR_POLL_DISCONNECT;

	drm_connector_helper_add(connector,
				 &max96745_bridge_connector_helper_funcs);

	ret = drm_connector_init(bridge->dev, connector,
				 &max96745_bridge_connector_funcs,
				 ser->next_bridge ? ser->next_bridge->type : bridge->type);
	if (ret) {
		DRM_ERROR("Failed to initialize connector\n");
		return ret;
	}

	if (max96745_bridge_link_locked(ser))
		connector->status = connector_status_connected;
	else
		connector->status = connector_status_disconnected;

	if (max96745_bridge_vid_tx_active(ser)) {
		extcon_set_state(ser->extcon, EXTCON_JACK_VIDEO_OUT, true);
		enable_irq(ser->lock.irq);
		ser->lock.irq_enabled = true;
	} else {
		extcon_set_state(ser->extcon, EXTCON_JACK_VIDEO_OUT, false);
	}

	drm_connector_attach_encoder(connector, bridge->encoder);

	return 0;
}

static void max96745_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);

	if (ser->panel)
		drm_panel_prepare(ser->panel);
}

static void max96745_bridge_enable(struct drm_bridge *bridge)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);

	regmap_update_bits(ser->regmap, 0x0100, VID_TX_EN,
			   FIELD_PREP(VID_TX_EN, 1));
	msleep(50);

	if (ser->panel)
		drm_panel_enable(ser->panel);

	extcon_set_state_sync(ser->extcon, EXTCON_JACK_VIDEO_OUT, true);

	if (!ser->lock.irq_enabled) {
		enable_irq(ser->lock.irq);
		ser->lock.irq_enabled = true;
	}
}

static void max96745_bridge_disable(struct drm_bridge *bridge)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);

	if (ser->lock.irq_enabled) {
		disable_irq(ser->lock.irq);
		ser->lock.irq_enabled = false;
	}

	extcon_set_state_sync(ser->extcon, EXTCON_JACK_VIDEO_OUT, false);

	if (ser->panel)
		drm_panel_disable(ser->panel);

	regmap_update_bits(ser->regmap, 0x0100, VID_TX_EN,
			   FIELD_PREP(VID_TX_EN, 0));
}

static void max96745_bridge_post_disable(struct drm_bridge *bridge)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);

	if (ser->panel)
		drm_panel_unprepare(ser->panel);
}

static enum drm_connector_status
max96745_bridge_detect(struct drm_bridge *bridge)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);
	struct drm_connector *connector = &ser->connector;

	if (!max96745_bridge_link_locked(ser))
		return connector_status_disconnected;

	if (connector->status == connector_status_connected) {
		u32 dprx_trn_status2;

		if (atomic_cmpxchg(&ser->lock.triggered, 1, 0))
			return connector_status_disconnected;

		if (regmap_read(ser->regmap, 0x641a, &dprx_trn_status2))
			return connector_status_disconnected;

		if ((dprx_trn_status2 & DPRX_TRAIN_STATE) != DPRX_TRAIN_STATE)
			return connector_status_disconnected;
	} else {
		atomic_set(&ser->lock.triggered, 0);
	}

	if (ser->next_bridge && (ser->next_bridge->ops & DRM_BRIDGE_OP_DETECT))
		return drm_bridge_detect(ser->next_bridge);

	return connector_status_connected;
}

static const struct drm_bridge_funcs max96745_bridge_funcs = {
	.attach = max96745_bridge_attach,
	.detect = max96745_bridge_detect,
	.pre_enable = max96745_bridge_pre_enable,
	.enable = max96745_bridge_enable,
	.disable = max96745_bridge_disable,
	.post_disable = max96745_bridge_post_disable,
	.atomic_get_input_bus_fmts = drm_atomic_helper_bridge_propagate_bus_fmt,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
};

static irqreturn_t max96745_bridge_lock_irq_handler(int irq, void *arg)
{
	struct max96745_bridge *ser = arg;

	atomic_set(&ser->lock.triggered, 1);

	return IRQ_HANDLED;
}

static int max96745_bridge_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct max96745_bridge *ser;
	int ret;

	ser = devm_kzalloc(dev, sizeof(*ser), GFP_KERNEL);
	if (!ser)
		return -ENOMEM;

	ser->dev = dev;
	platform_set_drvdata(pdev, ser);

	ser->regmap = dev_get_regmap(dev->parent, NULL);
	if (!ser->regmap)
		return dev_err_probe(dev, -ENODEV, "failed to get regmap\n");

	ser->lock.gpio = devm_gpiod_get(dev, "lock", GPIOD_IN);
	if (IS_ERR(ser->lock.gpio))
		return dev_err_probe(dev, PTR_ERR(ser->lock.gpio),
				     "failed to get lock GPIO\n");

	ser->extcon = devm_extcon_dev_allocate(dev, max96745_bridge_cable);
	if (IS_ERR(ser->extcon))
		return dev_err_probe(dev, PTR_ERR(ser->extcon),
				     "failed to allocate extcon device\n");

	ret = devm_extcon_dev_register(dev, ser->extcon);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register extcon device\n");

	ser->lock.irq = gpiod_to_irq(ser->lock.gpio);
	if (ser->lock.irq < 0)
		return ser->lock.irq;

	irq_set_status_flags(ser->lock.irq, IRQ_NOAUTOEN);
	ret = devm_request_threaded_irq(dev, ser->lock.irq, NULL,
					max96745_bridge_lock_irq_handler,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					dev_name(dev), ser);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request lock IRQ\n");

	ser->bridge.funcs = &max96745_bridge_funcs;
	ser->bridge.of_node = dev->of_node;
	ser->bridge.ops = DRM_BRIDGE_OP_DETECT;
	ser->bridge.type = DRM_MODE_CONNECTOR_LVDS;

	drm_bridge_add(&ser->bridge);

	return 0;
}

static int max96745_bridge_remove(struct platform_device *pdev)
{
	struct max96745_bridge *ser = platform_get_drvdata(pdev);

	drm_bridge_remove(&ser->bridge);

	return 0;
}

static const struct of_device_id max96745_bridge_of_match[] = {
	{ .compatible = "maxim,max96745-bridge", },
	{}
};
MODULE_DEVICE_TABLE(of, max96745_bridge_of_match);

static struct platform_driver max96745_bridge_driver = {
	.driver = {
		.name = "max96745-bridge",
		.of_match_table = of_match_ptr(max96745_bridge_of_match),
	},
	.probe = max96745_bridge_probe,
	.remove = max96745_bridge_remove,
};

module_platform_driver(max96745_bridge_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Maxim MAX96745 GMSL2 Serializer with eDP1.4a/DP1.4 Input");
MODULE_LICENSE("GPL");
