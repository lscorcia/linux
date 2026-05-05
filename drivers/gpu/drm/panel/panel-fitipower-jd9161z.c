// SPDX-License-Identifier: GPL-2.0+
/*
 * Author:
 * - Luca Leonardo Scorcia <l.scorcia@gmail.com>

 * This driver is based on Jadard JD9365DA-H3 - a very similar DSI controller.
 */

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

struct jd9161z;

struct jd9161z_panel_desc {
	const struct drm_display_mode mode;
	unsigned int lanes;
	enum mipi_dsi_pixel_format format;
	int (*init)(struct jd9161z *jd_data);
	bool lp11_before_reset;
	bool reset_before_power_off_vccio;
	unsigned int vccio_to_lp11_delay_ms;
	unsigned int lp11_to_reset_delay_ms;
	unsigned int backlight_off_to_display_off_delay_ms;
	unsigned int display_off_to_enter_sleep_delay_ms;
	unsigned int enter_sleep_to_reset_down_delay_ms;
	unsigned long mode_flags;
};

struct jd9161z {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	const struct jd9161z_panel_desc *desc;
	enum drm_panel_orientation orientation;
	struct regulator *vdd;
	struct regulator *vccio;
	struct gpio_desc *reset;
};

#define JD9161Z_DCS_SWITCH_PAGE		0xde

#define jd9161z_switch_page(dsi_ctx, page) \
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, JD9161Z_DCS_SWITCH_PAGE, (page))

static void jd9161z_enable_standard_cmds(struct mipi_dsi_multi_context *dsi_ctx)
{
	// Enable access to DCS and internal commands
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdf, 0x91, 0x62, 0xf3);
}

static inline struct jd9161z *panel_to_jd9161z(struct drm_panel *panel)
{
	return container_of(panel, struct jd9161z, panel);
}

static int jd9161z_disable(struct drm_panel *panel)
{
	struct jd9161z *jd_data = panel_to_jd9161z(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = jd_data->dsi };

	if (jd_data->desc->backlight_off_to_display_off_delay_ms)
		mipi_dsi_msleep(&dsi_ctx, jd_data->desc->backlight_off_to_display_off_delay_ms);

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);

	if (jd_data->desc->display_off_to_enter_sleep_delay_ms)
		mipi_dsi_msleep(&dsi_ctx, jd_data->desc->display_off_to_enter_sleep_delay_ms);

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);

	if (jd_data->desc->enter_sleep_to_reset_down_delay_ms)
		mipi_dsi_msleep(&dsi_ctx, jd_data->desc->enter_sleep_to_reset_down_delay_ms);

	return dsi_ctx.accum_err;
}

static int jd9161z_prepare(struct drm_panel *panel)
{
	struct jd9161z *jd_data = panel_to_jd9161z(panel);
	int ret;

	ret = regulator_enable(jd_data->vccio);
	if (ret)
		return ret;

	ret = regulator_enable(jd_data->vdd);
	if (ret)
		return ret;

	if (jd_data->desc->vccio_to_lp11_delay_ms)
		msleep(jd_data->desc->vccio_to_lp11_delay_ms);

	if (jd_data->desc->lp11_before_reset) {
		ret = mipi_dsi_dcs_nop(jd_data->dsi);
		if (ret)
			return ret;
	}

	if (jd_data->desc->lp11_to_reset_delay_ms)
		msleep(jd_data->desc->lp11_to_reset_delay_ms);

	gpiod_set_value_cansleep(jd_data->reset, 0);
	msleep(5);

	gpiod_set_value_cansleep(jd_data->reset, 1);
	msleep(10);

	gpiod_set_value_cansleep(jd_data->reset, 0);
	msleep(130);

	ret = jd_data->desc->init(jd_data);
	if (ret)
		return ret;

	return 0;
}

static int jd9161z_unprepare(struct drm_panel *panel)
{
	struct jd9161z *jd_data = panel_to_jd9161z(panel);

	gpiod_set_value_cansleep(jd_data->reset, 0);
	msleep(120);

	if (jd_data->desc->reset_before_power_off_vccio) {
		gpiod_set_value_cansleep(jd_data->reset, 1);

		usleep_range(1000, 2000);
	}

	regulator_disable(jd_data->vdd);
	regulator_disable(jd_data->vccio);

	return 0;
}

static int jd9161z_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct jd9161z *jd_data = panel_to_jd9161z(panel);
	const struct drm_display_mode *desc_mode = &jd_data->desc->mode;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, desc_mode);
	if (!mode) {
		DRM_DEV_ERROR(&jd_data->dsi->dev, "failed to add mode %ux%ux@%u\n",
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

static enum drm_panel_orientation jd9161z_panel_get_orientation(struct drm_panel *panel)
{
	struct jd9161z *jd_data = panel_to_jd9161z(panel);

	return jd_data->orientation;
}

static const struct drm_panel_funcs jd9161z_funcs = {
	.disable = jd9161z_disable,
	.unprepare = jd9161z_unprepare,
	.prepare = jd9161z_prepare,
	.get_modes = jd9161z_get_modes,
	.get_orientation = jd9161z_panel_get_orientation,
};

static int zhunyi_z40046_init_cmds_ctc(struct jd9161z *jd_data)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = jd_data->dsi };

	// Init configuration sequence
	jd9161z_switch_page(&dsi_ctx, 0x00);
	jd9161z_enable_standard_cmds(&dsi_ctx);

	// GAMMA_SET (pos/neg voltage of gamma power)
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7,
		0x10, 0x04, 0x86, 0x00, 0x1b, 0x35);

	// DCDC_SEL (power mode and charge pump settings)
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbb,
		0x69, 0x0b, 0x30, 0xb2, 0xb2, 0xc0, 0xe0, 0x20,
		0xf0, 0x50, 0x60);

	mipi_dsi_msleep(&dsi_ctx, 1);

	// VDDD_CTRL (control logic voltage setting)
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbc,
		0x73, 0x14);

	mipi_dsi_msleep(&dsi_ctx, 1);

	// SETRGBCYC (display waveform cycle of RGB mode)
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3,
		0x74, 0x04, 0x08, 0x0e, 0x00, 0x0e, 0x0c, 0x08,
		0x0e, 0x00, 0x0e, 0x82, 0x0a, 0x82);

	// SET_TCON (timing control setting)
	// param[0][5:4] + param[1]: number of panel lines / 2
	//   400 = 01 1001 0000 -> 0x10, 0x90
	// param[2]: scan line time width
	// param[3]: vfp: 14
	// param[4]: vs + vbp - 1: 11
	// param[5]: hbp: 4
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc4,
		0x10, 0x90, 0x92, 0x0e, 0x0b, 0x04);

	mipi_dsi_msleep(&dsi_ctx, 1);

	// SET_R_GAMMA (set red gamma output voltage)
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc8,
		0x7e, 0x76, 0x68, 0x57, 0x4c, 0x39, 0x3a, 0x23,
		0x3d, 0x3d, 0x40, 0x61, 0x54, 0x64, 0x5d, 0x62,
		0x5a, 0x50, 0x32, 0x7e, 0x76, 0x68, 0x57, 0x4c,
		0x39, 0x3a, 0x23, 0x3d, 0x3d, 0x40, 0x61, 0x54,
		0x64, 0x5d, 0x62, 0x5a, 0x50, 0x32);

	// SET_GIP_L (CGOUTx_L signal mapping, gs_panel = 0)
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd0,
		0x1f, 0x0a, 0x08, 0x06, 0x04, 0x1f, 0x00, 0x1f,
		0x17, 0x1f, 0x18, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f);

	// SET_GIP_R (CGOUTx_R signal mapping, gs_panel = 0)
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd1,
		0x1f, 0x0b, 0x09, 0x07, 0x05, 0x1f, 0x01, 0x1f,
		0x17, 0x1f, 0x18, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f);

	// SETGIP1 (GIP signal timing 1)
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd4,
		0x10, 0x00, 0x00, 0x03, 0x60, 0x05, 0x10, 0x00,
		0x02, 0x06, 0x68, 0x00, 0x6c, 0x00, 0x00, 0x00,
		0x00, 0x06, 0x78, 0x71, 0x07, 0x06, 0x68, 0x0c,
		0x25, 0x00, 0x63, 0x03, 0x00);

	// SETGIP2 (GIP signal timing 1)
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd5,
		0x20, 0x10, 0x8c, 0x18, 0x00, 0x80, 0x00, 0x08,
		0x00, 0x00, 0x06, 0x60, 0x00, 0x81, 0x70, 0x02,
		0x30, 0x01, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00,
		0x03, 0x60, 0x83, 0x90, 0x00, 0x00, 0x03, 0x4f,
		0x03, 0x00, 0x1f, 0x3f, 0x00, 0x00, 0x00, 0x00);

	jd9161z_switch_page(&dsi_ctx, 0x04);

	mipi_dsi_msleep(&dsi_ctx, 1);

	// Unknown command
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0,
		0x24, 0x01);

	mipi_dsi_msleep(&dsi_ctx, 1);

	jd9161z_switch_page(&dsi_ctx, 0x02);

	mipi_dsi_msleep(&dsi_ctx, 1);

	// SETRGBCYC2 (RGB IF source switch control timing)
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc1,
		0x71);

	mipi_dsi_msleep(&dsi_ctx, 1);

	// Unknown command
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc2,
		0x00, 0x18, 0x08, 0x1e, 0x25, 0x7c, 0xc7);

	mipi_dsi_msleep(&dsi_ctx, 1);

	jd9161z_switch_page(&dsi_ctx, 0x00);

	mipi_dsi_msleep(&dsi_ctx, 1);

	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);

	mipi_dsi_msleep(&dsi_ctx, 1);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);

	mipi_dsi_msleep(&dsi_ctx, 120);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	mipi_dsi_msleep(&dsi_ctx, 5);

	return dsi_ctx.accum_err;
};

static const struct jd9161z_panel_desc zhunyi_z40046_ctc_desc = {
	.mode = {
		.clock		= (480 + 20 + 20 + 20) * (800 + 14 + 4 + 8) * 60 / 1000,

		.hdisplay	= 480,
		.hsync_start	= 480 + 20,
		.hsync_end	= 480 + 20 + 20,
		.htotal		= 480 + 20 + 20 + 20,

		.vdisplay	= 800,
		.vsync_start	= 800 + 14,
		.vsync_end	= 800 + 14 + 4,
		.vtotal		= 800 + 14 + 4 + 8,

		.width_mm	= 52,
		.height_mm	= 86,
		.flags		= DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
		.type		= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	},
	.lanes = 2,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		      MIPI_DSI_MODE_LPM,
	.lp11_before_reset = true,
	.reset_before_power_off_vccio = true,
	.vccio_to_lp11_delay_ms = 5,
	.lp11_to_reset_delay_ms = 10,
	.backlight_off_to_display_off_delay_ms = 100,
	.display_off_to_enter_sleep_delay_ms = 50,
	.enter_sleep_to_reset_down_delay_ms = 100,
	.init = zhunyi_z40046_init_cmds_ctc,
};

static int jd9161z_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct jd9161z_panel_desc *desc;
	struct jd9161z *jd_data;
	int ret;

	jd_data = devm_drm_panel_alloc(dev, struct jd9161z, panel, &jd9161z_funcs,
				       DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(jd_data))
		return PTR_ERR(jd_data);

	desc = of_device_get_match_data(dev);

	if (desc->mode_flags)
		dsi->mode_flags = desc->mode_flags;
	else
		dsi->mode_flags = MIPI_DSI_MODE_VIDEO |
				  MIPI_DSI_MODE_VIDEO_BURST |
				  MIPI_DSI_MODE_NO_EOT_PACKET;

	dsi->format = desc->format;
	dsi->lanes = desc->lanes;

	jd_data->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(jd_data->reset))
		return dev_err_probe(&dsi->dev, PTR_ERR(jd_data->reset),
				     "failed to get our reset GPIO\n");

	jd_data->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(jd_data->vdd))
		return dev_err_probe(&dsi->dev, PTR_ERR(jd_data->vdd),
				     "failed to get vdd regulator\n");

	jd_data->vccio = devm_regulator_get(dev, "vccio");
	if (IS_ERR(jd_data->vccio))
		return dev_err_probe(&dsi->dev, PTR_ERR(jd_data->vccio),
				     "failed to get vccio regulator\n");

	ret = of_drm_get_panel_orientation(dev->of_node, &jd_data->orientation);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get orientation\n");

	ret = drm_panel_of_backlight(&jd_data->panel);
	if (ret)
		return ret;

	drm_panel_add(&jd_data->panel);

	mipi_dsi_set_drvdata(dsi, jd_data);
	jd_data->dsi = dsi;
	jd_data->desc = desc;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&jd_data->panel);

	return ret;
}

static void jd9161z_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct jd9161z *jd_data = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&jd_data->panel);
}

static const struct of_device_id jd9161z_of_match[] = {
	{
		// Init sequence obtained decompiling Xiaomi Mi Smart Clock
		// x04g stock kernel
		.compatible = "zhunyikeji,z40046-ctc",
		.data = &zhunyi_z40046_ctc_desc
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jd9161z_of_match);

static struct mipi_dsi_driver jd9161z_driver = {
	.probe = jd9161z_dsi_probe,
	.remove = jd9161z_dsi_remove,
	.driver = {
		.name = "fitipower-jd9161z",
		.of_match_table = jd9161z_of_match,
	},
};
module_mipi_dsi_driver(jd9161z_driver);

MODULE_AUTHOR("Luca Leonardo Scorcia <l.scorcia@gmail.com>");
MODULE_DESCRIPTION("Fitipower JD9161Z-based WVGA DSI panels");
MODULE_LICENSE("GPL");
