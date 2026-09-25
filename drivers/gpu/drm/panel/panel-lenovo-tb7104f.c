// SPDX-License-Identifier: GPL-2.0+
/*
 * Author:
 * - Luca Leonardo Scorcia <l.scorcia@gmail.com>

 * This driver is based on stock android sources for Lenovo Tab E7 (TB7104-F).
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

/* Manufacturer specific DSI commands */
#define LENOVO_UNKNOWN_A8		0xA8
#define LENOVO_UNKNOWN_89		0x89
#define LENOVO_UNKNOWN_95		0x95
#define LENOVO_UNKNOWN_B9		0xB9
#define LENOVO_UNKNOWN_BB		0xBB

struct lenovo {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	const struct lenovo_panel_desc *desc;

	enum drm_panel_orientation orientation;
	struct regulator *vdd;
	struct gpio_desc *reset;
	struct gpio_desc *enable;
};

struct lenovo_panel_desc {
	const struct drm_display_mode mode;

	unsigned int lanes;
	enum mipi_dsi_pixel_format format;
	unsigned long mode_flags;

	int (*init)(struct lenovo *ctx);
};

static inline struct lenovo *panel_to_lenovo(struct drm_panel *panel)
{
	return container_of(panel, struct lenovo, base);
}

static int lenovo_prepare(struct drm_panel *panel)
{
	struct lenovo *ctx = panel_to_lenovo(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	int ret;

	pr_err("*** LUCA lenovo_prepare_1");

	ret = regulator_enable(ctx->vdd);
	if (ret)
		return ret;

	msleep(10);

	// Enter sleep mode to avoid white screen during reset
	// Requires 120msec wait minimum
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	msleep(120);

	// Assert reset line for 10usec minimum
	gpiod_set_value_cansleep(ctx->reset, 1);
	usleep_range(10, 20);

	// Wait 5 msec minimum after deasserting reset before sending commands
	gpiod_set_value_cansleep(ctx->reset, 0);
	msleep(5);

	pr_err("*** LUCA lenovo_prepare_3");

	return 0;

disable_vdd:
	regulator_disable(ctx->vdd);

	return ret;
}

static int lenovo_enable(struct drm_panel *panel)
{
	struct lenovo *ctx = panel_to_lenovo(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	int ret;

	pr_err("*** LUCA lenovo_enable_1");

	ret = ctx->desc->init(ctx);
	//if (ret)
	//	return ret;

	pr_err("*** LUCA lenovo_enable_2");

	// Exit sleep mode to start display clocks
	// Requires 120msec wait minimum
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	pr_err("*** LUCA lenovo_enable_3");

	// Turn on the display
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	pr_err("*** LUCA lenovo_enable_4");

	return 0;
}

static int lenovo_disable(struct drm_panel *panel)
{
	struct lenovo *ctx = panel_to_lenovo(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	pr_err("*** LUCA lenovo_disable_1");

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);

	pr_err("*** LUCA lenovo_disable_2");

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);

	pr_err("*** LUCA lenovo_disable_3");

	return dsi_ctx.accum_err;
}

static int lenovo_unprepare(struct drm_panel *panel)
{
	struct lenovo *ctx = panel_to_lenovo(panel);

	pr_err("*** LUCA lenovo_unprepare_1");

	regulator_disable(ctx->vdd);

	pr_err("*** LUCA lenovo_unprepare_2");

	return 0;
}

static int lenovo_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct lenovo *ctx = panel_to_lenovo(panel);
	const struct drm_display_mode *desc_mode = &ctx->desc->mode;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, desc_mode);
	if (!mode) {
		DRM_DEV_ERROR(&ctx->dsi->dev, "failed to add mode %ux%ux@%u\n",
			      desc_mode->hdisplay, desc_mode->vdisplay,
			      drm_mode_vrefresh(desc_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	return 1;
}

static enum drm_panel_orientation lenovo_panel_get_orientation(struct drm_panel *panel)
{
	struct lenovo *ctx = panel_to_lenovo(panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs lenovo_funcs = {
	.prepare = lenovo_prepare,
	.enable = lenovo_enable,
	.disable = lenovo_disable,
	.unprepare = lenovo_unprepare,
	.get_modes = lenovo_get_modes,
	.get_orientation = lenovo_panel_get_orientation,
};

static int lenovo_tb7104f_panel_init(struct lenovo *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	pr_err("*** LUCA lenovo_tb7104f_panel_init");

	// Init configuration sequence
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, LENOVO_UNKNOWN_89, 0xD6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, LENOVO_UNKNOWN_B9, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, LENOVO_UNKNOWN_BB, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, LENOVO_UNKNOWN_95, 0x1A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, LENOVO_UNKNOWN_89, 0xD5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, LENOVO_UNKNOWN_A8, 0x08);

	return dsi_ctx.accum_err;
};

static const struct lenovo_panel_desc lenovo_tb7104f_panel_desc = {
	.mode = {
		.clock		= (1024 + 60 + 60 + 10) * (600 + 20 + 18 + 2) * 60 / 1000,

		.hdisplay 	= 1024,
		.hsync_start 	= 1024 + 160,
		.hsync_end 	= 1024 + 160 + 160,
		.htotal 	= 1024 + 160 + 160 + 60,

		.vdisplay 	= 600,
		.vsync_start 	= 600 + 12,
		.vsync_end 	= 600 + 12 + 23,
		.vtotal 	= 600 + 12 + 23 + 1,

		.width_mm	= 154,
		.height_mm	= 86,
		.flags		= DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
		.type		= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	},
	.lanes = 3,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST,
	.init = lenovo_tb7104f_panel_init,
};

static int lenovo_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct lenovo_panel_desc *desc;
	struct lenovo *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct lenovo, base, &lenovo_funcs,
				       DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	desc = of_device_get_match_data(dev);

	if (desc->mode_flags)
		dsi->mode_flags = desc->mode_flags;
	else
		dsi->mode_flags = MIPI_DSI_MODE_VIDEO |
				  MIPI_DSI_MODE_VIDEO_BURST |
				  MIPI_DSI_MODE_NO_EOT_PACKET;

	dsi->format = desc->format;
	dsi->lanes = desc->lanes;

	ctx->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset))
		return dev_err_probe(&dsi->dev, PTR_ERR(ctx->reset),
				     "failed to get our reset GPIO\n");

	ctx->enable = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->enable))
		return dev_err_probe(&dsi->dev, PTR_ERR(ctx->enable),
				     "failed to get our enable GPIO\n");

	ctx->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(ctx->vdd))
		return dev_err_probe(&dsi->dev, PTR_ERR(ctx->vdd),
				     "failed to get vdd regulator\n");

	ret = drm_of_get_panel_orientation(dev->of_node, &ctx->orientation);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get orientation\n");

	ret = drm_panel_of_backlight(&ctx->base);
	if (ret)
		return ret;

	ctx->base.prepare_prev_first = true;

	drm_panel_add(&ctx->base);

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;
	ctx->desc = desc;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->base);

	return ret;
}

static void lenovo_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct lenovo *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->base);
}

static const struct of_device_id lenovo_of_match[] = {
	{
		// Init sequence obtained decompiling Xiaomi Mi Smart Clock
		// x04g stock kernel
		.compatible = "lenovo,tb7104f-panel",
		.data = &lenovo_tb7104f_panel_desc
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lenovo_of_match);

static struct mipi_dsi_driver lenovo_driver = {
	.probe = lenovo_dsi_probe,
	.remove = lenovo_dsi_remove,
	.driver = {
		.name = "lenovo-tb7104f-panel",
		.of_match_table = lenovo_of_match,
	},
};
module_mipi_dsi_driver(lenovo_driver);

MODULE_AUTHOR("Luca Leonardo Scorcia <l.scorcia@gmail.com>");
MODULE_DESCRIPTION("Lenovo TB7104F DSI panel");
MODULE_LICENSE("GPL");
