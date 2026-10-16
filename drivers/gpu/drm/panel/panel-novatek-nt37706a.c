// SPDX-License-Identifier: GPL-2.0-only
/*
 * Novatek NT37706A BOE AMOLED panel (Nothing Phone (4a)), DSC video mode.
 * Init sequence and timings from the vendor device tree; structure follows
 * panel-novatek-nt37705.c.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct nt37706a_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

/* Power-on order; delays are the vendor's post-on sleeps */
static const struct regulator_bulk_data nt37706a_supplies[] = {
	{ .supply = "vddio" },
	{ .supply = "dvdd" },
	{ .supply = "vci" },
};

static const unsigned int nt37706a_post_on_ms[] = { 2, 2, 10 };

static inline struct nt37706a_panel *to_nt37706a_panel(struct drm_panel *panel)
{
	return container_of_const(panel, struct nt37706a_panel, panel);
}

static int nt37706a_power_on(struct nt37706a_panel *ctx)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(nt37706a_supplies); i++) {
		ret = regulator_enable(ctx->supplies[i].consumer);
		if (ret < 0) {
			while (--i >= 0)
				regulator_disable(ctx->supplies[i].consumer);
			return ret;
		}
		msleep(nt37706a_post_on_ms[i]);
	}

	return 0;
}

static void nt37706a_power_off(struct nt37706a_panel *ctx)
{
	int i;

	for (i = ARRAY_SIZE(nt37706a_supplies) - 1; i >= 0; i--) {
		regulator_disable(ctx->supplies[i].consumer);
		usleep_range(2000, 3000);
	}
}

static void nt37706a_reset(struct nt37706a_panel *ctx)
{
	/* Vendor sequence: high 1 ms, low 1 ms, high 10 ms (reset is active low) */
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int nt37706a_on(struct nt37706a_panel *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7, 0x1c, 0x02, 0x1c, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xaa, 0x55, 0xa5, 0x84);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x27);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf7, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0xa9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf4, 0xf1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf2, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2a, 0x00, 0x00, 0x04, 0xc7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2b, 0x00, 0x00, 0x0a, 0x9f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90, 0x03, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x91,
				     0xab, 0xa8, 0x00, 0x28, 0xe2, 0x00, 0x02,
				     0x65, 0x03, 0xc7, 0x00, 0x08, 0x02, 0xe0,
				     0x02, 0x3f, 0x10, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3b, 0x00, 0x30, 0x00, 0x4c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3b, 0x00, 0x30, 0x04, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3b, 0x00, 0x30, 0x0b, 0x68);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3b, 0x00, 0x30, 0x00, 0x4c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x88, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5f, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2f, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xea, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xea, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc1, 0x0f, 0x29, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x91);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x29);

	return dsi_ctx.accum_err;
}

static int nt37706a_off(struct nt37706a_panel *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x28);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10);
	mipi_dsi_msleep(&dsi_ctx, 100);

	return dsi_ctx.accum_err;
}

static int nt37706a_prepare(struct drm_panel *panel)
{
	struct nt37706a_panel *ctx = to_nt37706a_panel(panel);
	struct device *dev = &ctx->dsi->dev;
	struct drm_dsc_picture_parameter_set pps;
	int ret;

	ret = nt37706a_power_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	nt37706a_reset(ctx);

	ret = nt37706a_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		goto err_power_off;
	}

	drm_dsc_pps_payload_pack(&pps, &ctx->dsc);

	ret = mipi_dsi_picture_parameter_set(ctx->dsi, &pps);
	if (ret < 0) {
		dev_err(dev, "Failed to transmit PPS: %d\n", ret);
		goto err_power_off;
	}

	ret = mipi_dsi_compression_mode(ctx->dsi, true);
	if (ret < 0) {
		dev_err(dev, "Failed to enable compression mode: %d\n", ret);
		goto err_power_off;
	}

	return 0;

err_power_off:
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	nt37706a_power_off(ctx);
	return ret;
}

static int nt37706a_unprepare(struct drm_panel *panel)
{
	struct nt37706a_panel *ctx = to_nt37706a_panel(panel);
	int ret;

	ret = nt37706a_off(ctx);
	if (ret < 0)
		dev_err(&ctx->dsi->dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	nt37706a_power_off(ctx);

	return 0;
}

static const struct drm_display_mode nt37706a_mode = {
	.clock = (1224 + 41 + 16 + 16) * (2720 + 76 + 2 + 46) * 120 / 1000,
	.hdisplay = 1224,
	.hsync_start = 1224 + 41,
	.hsync_end = 1224 + 41 + 16,
	.htotal = 1224 + 41 + 16 + 16,
	.vdisplay = 2720,
	.vsync_start = 2720 + 76,
	.vsync_end = 2720 + 76 + 2,
	.vtotal = 2720 + 76 + 2 + 46,
	.width_mm = 71,
	.height_mm = 157,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int nt37706a_get_modes(struct drm_panel *panel,
			      struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &nt37706a_mode);
}

static const struct drm_panel_funcs nt37706a_panel_funcs = {
	.prepare = nt37706a_prepare,
	.unprepare = nt37706a_unprepare,
	.get_modes = nt37706a_get_modes,
};

static int nt37706a_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static const struct backlight_ops nt37706a_bl_ops = {
	.update_status = nt37706a_bl_update_status,
};

static struct backlight_device *
nt37706a_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.scale = BACKLIGHT_SCALE_NON_LINEAR,
		.brightness = 1600,
		.max_brightness = 4095,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &nt37706a_bl_ops, &props);
}

static int nt37706a_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct nt37706a_panel *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct nt37706a_panel, panel,
				   &nt37706a_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(nt37706a_supplies),
					    nt37706a_supplies,
					    &ctx->supplies);
	if (ret < 0)
		return ret;

	/* Keep reset deasserted so the bootloader image survives until modeset */
	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB101010;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS |
			  MIPI_DSI_MODE_DSC_ALL_SLICES_IN_PKT;

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = nt37706a_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	/* This panel only supports DSC; unconditionally enable it */
	dsi->dsc = &ctx->dsc;

	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 2;

	ctx->dsc.slice_height = 40;
	ctx->dsc.slice_width = 612;
	ctx->dsc.slice_count = 1224 / ctx->dsc.slice_width;
	ctx->dsc.bits_per_component = 10;
	ctx->dsc.bits_per_pixel = 8 << 4; /* 4 fractional bits */
	ctx->dsc.block_pred_enable = true;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void nt37706a_remove(struct mipi_dsi_device *dsi)
{
	struct nt37706a_panel *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id nt37706a_of_match[] = {
	{ .compatible = "nothing,frogger-panel-boe" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nt37706a_of_match);

static struct mipi_dsi_driver nt37706a_driver = {
	.probe = nt37706a_probe,
	.remove = nt37706a_remove,
	.driver = {
		.name = "panel-novatek-nt37706a",
		.of_match_table = nt37706a_of_match,
	},
};
module_mipi_dsi_driver(nt37706a_driver);

MODULE_DESCRIPTION("DRM driver for the NT37706A BOE panel in the Nothing Phone (4a)");
MODULE_LICENSE("GPL");
