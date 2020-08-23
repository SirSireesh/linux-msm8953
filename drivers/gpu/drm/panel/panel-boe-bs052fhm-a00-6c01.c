// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020, The Linux Foundation. All rights reserved.
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct bs052fhm_a00_6c01 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;
	struct backlight_device *backlight;
	bool prepared;
};

static inline struct bs052fhm_a00_6c01 *to_bs052fhm_a00_6c01(struct drm_panel *panel)
{
	return container_of(panel, struct bs052fhm_a00_6c01, panel);
}

#define dsi_dcs_write_seq(dsi, seq...) do {				\
		static const u8 d[] = { seq };				\
		int ret;						\
		ret = mipi_dsi_dcs_write_buffer(dsi, d, ARRAY_SIZE(d));	\
		if (ret < 0)						\
			return ret;					\
	} while (0)

static void bs052fhm_a00_6c01_reset(struct bs052fhm_a00_6c01 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);
	msleep(20);
}

static int bs052fhm_a00_6c01_on(struct bs052fhm_a00_6c01 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi_dcs_write_seq(dsi, 0xff, 0xee);
	dsi_dcs_write_seq(dsi, 0x18, 0x40);
	usleep_range(10000, 11000);
	dsi_dcs_write_seq(dsi, 0x18, 0x00);
	msleep(20);
	dsi_dcs_write_seq(dsi, 0xff, 0x01);
	dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	dsi_dcs_write_seq(dsi, 0x60, 0x0f);
	dsi_dcs_write_seq(dsi, 0x6d, 0x33);
	dsi_dcs_write_seq(dsi, 0x58, 0x82);
	dsi_dcs_write_seq(dsi, 0x59, 0x00);
	dsi_dcs_write_seq(dsi, 0x5a, 0x02);
	dsi_dcs_write_seq(dsi, 0x5b, 0x00);
	dsi_dcs_write_seq(dsi, 0x5c, 0x82);
	dsi_dcs_write_seq(dsi, 0x5d, 0x80);
	dsi_dcs_write_seq(dsi, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS, 0x02);
	dsi_dcs_write_seq(dsi, MIPI_DCS_GET_CABC_MIN_BRIGHTNESS, 0x00);
	dsi_dcs_write_seq(dsi, 0x1b, 0x1b);
	dsi_dcs_write_seq(dsi, 0x1c, 0xf7);
	dsi_dcs_write_seq(dsi, 0x66, 0x01);
	dsi_dcs_write_seq(dsi, 0xff, 0x05);
	dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	dsi_dcs_write_seq(dsi, 0xa6, 0x04);
	dsi_dcs_write_seq(dsi, 0xff, 0xff);
	dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	dsi_dcs_write_seq(dsi, 0x4f, 0x03);
	dsi_dcs_write_seq(dsi, 0xff, 0x05);
	dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	dsi_dcs_write_seq(dsi, 0x86, 0x1b);
	dsi_dcs_write_seq(dsi, 0x87, 0x39);
	dsi_dcs_write_seq(dsi, 0x88, 0x1b);
	dsi_dcs_write_seq(dsi, 0x89, 0x39);
	dsi_dcs_write_seq(dsi, 0x8c, 0x01);
	dsi_dcs_write_seq(dsi, 0xb5, 0x20);
	dsi_dcs_write_seq(dsi, 0xff, 0x00);
	dsi_dcs_write_seq(dsi, 0xfb, 0x01);

	ret = mipi_dsi_dcs_set_display_brightness(dsi, 0x0fff);
	if (ret < 0) {
		dev_err(dev, "Failed to set display brightness: %d\n", ret);
		return ret;
	}

	dsi_dcs_write_seq(dsi, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS, 0x00);
	dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x2c);
	dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_POWER_SAVE, 0x01);
	dsi_dcs_write_seq(dsi, 0x34, 0x00);
	dsi_dcs_write_seq(dsi, 0xd3, 0x06);
	dsi_dcs_write_seq(dsi, 0xd4, 0x04);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	dsi_dcs_write_seq(dsi, 0xff, 0x01);
	dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	dsi_dcs_write_seq(dsi, MIPI_DCS_GET_SIGNAL_MODE, 0xb0);
	dsi_dcs_write_seq(dsi, MIPI_DCS_GET_DIAGNOSTIC_RESULT, 0xa9);
	dsi_dcs_write_seq(dsi, 0xff, 0x00);
	dsi_dcs_write_seq(dsi, 0xfb, 0x01);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	usleep_range(10000, 11000);

	return 0;
}

static int bs052fhm_a00_6c01_off(struct bs052fhm_a00_6c01 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	backlight_disable(ctx->backlight);

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	msleep(35);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	return 0;
}

static int bs052fhm_a00_6c01_prepare(struct drm_panel *panel)
{
	struct bs052fhm_a00_6c01 *ctx = to_bs052fhm_a00_6c01(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	bs052fhm_a00_6c01_reset(ctx);

	ret = bs052fhm_a00_6c01_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
		return ret;
	}

	ctx->prepared = true;
	return 0;
}

static int bs052fhm_a00_6c01_unprepare(struct drm_panel *panel)
{
	struct bs052fhm_a00_6c01 *ctx = to_bs052fhm_a00_6c01(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = bs052fhm_a00_6c01_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	ctx->prepared = false;
	return 0;
}

static int bs052fhm_a00_6c01_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	bl->props.brightness = brightness;

	return brightness;
}

static int bs052fhm_a00_6c01_set_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);

	int ret = mipi_dsi_dcs_set_display_brightness(dsi, bl->props.brightness);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct backlight_ops bs052fhm_a00_6c01_ops = {
	.update_status = bs052fhm_a00_6c01_set_brightness,
	.get_brightness = bs052fhm_a00_6c01_get_brightness,
};

static const struct drm_display_mode bs052fhm_a00_6c01_mode = {
	.clock = (1080 + 72 + 4 + 16) * (1920 + 4 + 2 + 4) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 72,
	.hsync_end = 1080 + 72 + 4,
	.htotal = 1080 + 72 + 4 + 16,
	.vdisplay = 1920,
	.vsync_start = 1920 + 4,
	.vsync_end = 1920 + 4 + 2,
	.vtotal = 1920 + 4 + 2 + 4,
	.vrefresh = 60,
	.width_mm = 68,
	.height_mm = 121,
};

static int bs052fhm_a00_6c01_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &bs052fhm_a00_6c01_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs bs052fhm_a00_6c01_panel_funcs = {
	.prepare = bs052fhm_a00_6c01_prepare,
	.unprepare = bs052fhm_a00_6c01_unprepare,
	.get_modes = bs052fhm_a00_6c01_get_modes,
};

static int bs052fhm_a00_6c01_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct bs052fhm_a00_6c01 *ctx;
	struct backlight_properties bl_props;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supplies[0].supply = "lab";
	ctx->supplies[1].supply = "ibb";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to get regulators: %d\n", ret);
		return ret;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		ret = PTR_ERR(ctx->reset_gpio);
		dev_err(dev, "Failed to get reset-gpios: %d\n", ret);
		return ret;
	}

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &bs052fhm_a00_6c01_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	memset(&bl_props, 0, sizeof(bl_props));
	bl_props.type = BACKLIGHT_RAW;
	bl_props.brightness = 0x0fff;
	bl_props.max_brightness = 0x0fff;

	ctx->backlight = devm_backlight_device_register(dev, "bs052fhm-a00-6c01",
						dev, dsi, &bs052fhm_a00_6c01_ops,
						&bl_props);
	if (IS_ERR(ctx->backlight)) {
		dev_err(dev, "Failed to register backlight, err=%d\n", ret);
		return PTR_ERR(ctx->backlight);
	}

	ret = drm_panel_add(&ctx->panel);
	if (ret < 0) {
		dev_err(dev, "Failed to add panel: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		return ret;
	}

	return 0;
}

static int bs052fhm_a00_6c01_remove(struct mipi_dsi_device *dsi)
{
	struct bs052fhm_a00_6c01 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id bs052fhm_a00_6c01_of_match[] = {
	{ .compatible = "boe,bs052fhm-a00-6c01" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, bs052fhm_a00_6c01_of_match);

static struct mipi_dsi_driver bs052fhm_a00_6c01_driver = {
	.probe = bs052fhm_a00_6c01_probe,
	.remove = bs052fhm_a00_6c01_remove,
	.driver = {
		.name = "panel-boe-bs052fhm-a00-6c01",
		.of_match_table = bs052fhm_a00_6c01_of_match,
	},
};
module_mipi_dsi_driver(bs052fhm_a00_6c01_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <sireeshkodali1@gmail.com>");
MODULE_DESCRIPTION("DRM driver for Boe BS052FHM-A00-6C01");
MODULE_LICENSE("GPL v2");
