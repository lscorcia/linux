// SPDX-License-Identifier: GPL-2.0+
/*
 * Author:
 * - Luca Leonardo Scorcia <l.scorcia@gmail.com>

 * This driver is based on Jadard JD9365DA-H3 - a very similar DSI controller.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

/* Manufacturer specific DSI commands */
#define JD9161Z_PG0_SEQUENCE_CTRL	0xb0 /* Power on sequence control */
#define JD9161Z_PG0_VCOM_SET		0xb2 /* Set VCOM voltage for normal scan direction */
#define JD9161Z_PG0_VCOM_R_SET		0xb3 /* Set VCOM voltage for reverse scan direction */
#define JD9161Z_PG0_GAMMA_SET_OTP_TIMES	0xb5
#define JD9161Z_PG0_GAMMA_SET		0xb7 /* Pos/neg voltage of gamma power */
#define JD9161Z_PG0_OTP_SET		0xb8 /* Set internal OTP program related settings */
#define JD9161Z_PG0_POWER_CTRL2		0xb9 /* Set power function related settings */
#define JD9161Z_PG0_POWER_STATE		0xba /* Power status settings */
#define JD9161Z_PG0_DCDC_SEL		0xbb /* Power mode and charge pump settings */
#define JD9161Z_PG0_VDDD_CTRL		0xbc /* Control logic voltage setting */
#define JD9161Z_PG0_GAS_CTRL		0xbe /* GAS function control */
#define JD9161Z_PG0_SETSTBA		0xc0 /* Set Source Output driving ability */
#define JD9161Z_PG0_SETPANEL		0xc1 /* Set panel related register */
#define JD9161Z_PG0_SET_BIST		0xc2 /* BIST pattern setting */
#define JD9161Z_PG0_SETRGBCYC		0xc3 /* Display waveform cycle of RGB mode */
#define JD9161Z_PG0_SET_TCON		0xc4 /* Timing control setting */
#define JD9161Z_PG0_SET_R_GAMMA		0xc8 /* Set red gamma output voltage */
#define JD9161Z_PG0_SET_GIP_L		0xd0 /* CGOUTx_L signal mapping, gs_panel = 0 */
#define JD9161Z_PG0_SET_GIP_R		0xd1 /* CGOUTx_R signal mapping, gs_panel = 0 */
#define JD9161Z_PG0_SET_GIP_L_GS	0xd2 /* CGOUTx_L signal mapping, gs_panel = 1 */
#define JD9161Z_PG0_SET_GIP_R_GS	0xd3 /* CGOUTx_R signal mapping, gs_panel = 1 */
#define JD9161Z_PG0_SETGIP1		0xd4 /* GIP signal timing 1 */
#define JD9161Z_PG0_SETGIP2		0xd5 /* GIP signal timing 2 */
#define JD9161Z_PG0_AUTO_DISP_SETTING	0xd8
#define JD9161Z_PG0_OTG_PROG		0xd9 /* OTG program */
#define JD9161Z_PG0_READ_ID1		0xda /* Read ID1 */
#define JD9161Z_PG0_READ_ID2		0xdb /* Read ID2 */
#define JD9161Z_PG0_READ_ID3		0xdc /* Read ID3 */
#define JD9161Z_PG0_SET_WD		0xdd /* Setup watchdog */
#define JD9161Z_PG0_SET_PAGE		0xde /* Command page switch */
#define JD9161Z_PG0_SET_PASSWD		0xdf /* Unlock manufacturer commands */
#define JD9161Z_PG0_SETDDB		0xe5 /* Set device descriptor block */
#define JD9161Z_PG0_OTP_START_CTRL	0xe8

#define JD9161Z_PG1_PWM_CTRL		0xb5 /* PWM control */
#define JD9161Z_PG1_CABC_VLD		0xb6 /* CABC valid */
#define JD9161Z_PG1_BC_DIM_FT_CTRL	0xbb

#define JD9161Z_PG2_GAMMA_POWER_TEST	0xbe /* Set gamma power related settings */
#define JD9161Z_PG2_SETRGBCYC2		0xc1 /* RGB IF source switch control timing */
#define JD9161Z_PG2_UNKNOWN_C2		0xc2
#define JD9161Z_PG2_SET_OSCM		0xc5 /* Oscillator M setting */

#define JD9161Z_PG4_UNKNOWN_B0		0xb0

struct jd9161z {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	const struct jd9161z_panel_desc *desc;

	enum drm_panel_orientation orientation;
	struct regulator *vdd;
	struct regulator *vccio;
	struct gpio_desc *reset;
};

struct jd9161z_panel_desc {
	const struct drm_display_mode mode;

	unsigned int lanes;
	enum mipi_dsi_pixel_format format;
	bool lp11_before_reset;
	bool reset_before_power_off_vccio;
	unsigned int vccio_to_lp11_delay_ms;
	unsigned int lp11_to_reset_delay_ms;
	unsigned int backlight_off_to_display_off_delay_ms;
	unsigned int display_off_to_enter_sleep_delay_ms;
	unsigned int enter_sleep_to_reset_down_delay_ms;
	unsigned long mode_flags;

	int (*init)(struct jd9161z *ctx);
};

static inline struct jd9161z *panel_to_jd9161z(struct drm_panel *panel)
{
	return container_of(panel, struct jd9161z, base);
}

#define jd9161z_switch_page(dsi_ctx, page) \
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, JD9161Z_PG0_SET_PAGE, (page))

static void jd9161z_enable_extended_cmds(struct mipi_dsi_multi_context *dsi_ctx)
{
	// Enable access to DCS and internal commands
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, JD9161Z_PG0_SET_PASSWD,
		0x91, 0x62, 0xf3);
}

static int jd9161z_read_otp_id(struct mipi_dsi_multi_context *dsi_ctx)
{
	struct mipi_dsi_device *dsi = dsi_ctx->dsi;
	u8 id1, id2, id3;
	int ret;

	ret = mipi_dsi_dcs_read(dsi, JD9161Z_PG0_READ_ID1, &id1, 1);
	if (ret < 0) {
		dev_err(&dsi->dev, "Could not read OTP ID1\n");
		return ret;
	}

	ret = mipi_dsi_dcs_read(dsi, JD9161Z_PG0_READ_ID2, &id2, 1);
	if (ret < 0) {
		dev_err(&dsi->dev, "Could not read OTP ID2\n");
		return ret;
	}

	ret = mipi_dsi_dcs_read(dsi, JD9161Z_PG0_READ_ID3, &id3, 1);
	if (ret < 0) {
		dev_err(&dsi->dev, "Could not read OTP ID3\n");
		return ret;
	}

	/*
	 * One-Time Programmable (?) memory contains manufacturer
	 * ID (e.g. Fitipower 0x91), driver ID (e.g. JD9161 0x61) and
	 * version (eg. 0x1a).
	 */
	dev_info(&dsi->dev, "OTP ID manufacturer: %02x version: %02x driver: %02x\n", id1, id2, id3);

	return 0;
}

static int jd9161z_prepare(struct drm_panel *panel)
{
	struct jd9161z *ctx = panel_to_jd9161z(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	int ret;

	pr_err("*** LUCA jd9161z_prepare_1");

	ret = regulator_enable(ctx->vccio);
	if (ret)
		return ret;

	ret = regulator_enable(ctx->vdd);
	if (ret)
		goto disable_vccio;

	msleep(10);

	pr_err("*** LUCA jd9161z_prepare_2");

/*
	if (ctx->desc->vccio_to_lp11_delay_ms)
		msleep(ctx->desc->vccio_to_lp11_delay_ms);

	if (ctx->desc->lp11_before_reset) {
		ret = mipi_dsi_dcs_nop(ctx->dsi);
		if (ret)
			goto disable_vdd;
	}

	if (ctx->desc->lp11_to_reset_delay_ms)
		msleep(ctx->desc->lp11_to_reset_delay_ms);
*/

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

	pr_err("*** LUCA jd9161z_prepare_3");

	ret = jd9161z_read_otp_id(&dsi_ctx);
	if (ret)
		goto disable_vdd;

	return 0;

disable_vdd:
	regulator_disable(ctx->vdd);

disable_vccio:
	regulator_disable(ctx->vccio);

	return ret;
}

static int jd9161z_enable(struct drm_panel *panel)
{
	struct jd9161z *ctx = panel_to_jd9161z(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	int ret;

	pr_err("*** LUCA jd9161z_enable_1");

	ret = ctx->desc->init(ctx);
	if (ret)
		return ret;

	pr_err("*** LUCA jd9161z_enable_2");

	// Exit sleep mode to start display clocks
	// Requires 120msec wait minimum
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	pr_err("*** LUCA jd9161z_enable_3");

	// Turn on the display
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	pr_err("*** LUCA jd9161z_enable_4");

	return 0;
}

static int jd9161z_disable(struct drm_panel *panel)
{
	struct jd9161z *ctx = panel_to_jd9161z(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	if (ctx->desc->backlight_off_to_display_off_delay_ms)
		mipi_dsi_msleep(&dsi_ctx, ctx->desc->backlight_off_to_display_off_delay_ms);

	pr_err("*** LUCA jd9161z_disable_1");

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);

	pr_err("*** LUCA jd9161z_disable_2");

	if (ctx->desc->display_off_to_enter_sleep_delay_ms)
		mipi_dsi_msleep(&dsi_ctx, ctx->desc->display_off_to_enter_sleep_delay_ms);

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);

	if (ctx->desc->enter_sleep_to_reset_down_delay_ms)
		mipi_dsi_msleep(&dsi_ctx, ctx->desc->enter_sleep_to_reset_down_delay_ms);

	pr_err("*** LUCA jd9161z_disable_3");

	return dsi_ctx.accum_err;
}

static int jd9161z_unprepare(struct drm_panel *panel)
{
	struct jd9161z *ctx = panel_to_jd9161z(panel);
/*
	gpiod_set_value_cansleep(ctx->reset, 0);
	msleep(120);

	if (ctx->desc->reset_before_power_off_vccio) {
		gpiod_set_value_cansleep(ctx->reset, 1);

		usleep_range(1000, 2000);
	}
*/
	pr_err("*** LUCA jd9161z_unprepare_1");

	regulator_disable(ctx->vdd);
	regulator_disable(ctx->vccio);

	pr_err("*** LUCA jd9161z_unprepare_2");

	return 0;
}

static int jd9161z_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct jd9161z *ctx = panel_to_jd9161z(panel);
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

static enum drm_panel_orientation jd9161z_panel_get_orientation(struct drm_panel *panel)
{
	struct jd9161z *ctx = panel_to_jd9161z(panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs jd9161z_funcs = {
	.prepare = jd9161z_prepare,
	.enable = jd9161z_enable,
	.disable = jd9161z_disable,
	.unprepare = jd9161z_unprepare,
	.get_modes = jd9161z_get_modes,
	.get_orientation = jd9161z_panel_get_orientation,
};

static int zhunyi_z40046_init_cmds_ctc(struct jd9161z *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	// Init configuration sequence
	jd9161z_switch_page(&dsi_ctx, 0x00);
	jd9161z_enable_extended_cmds(&dsi_ctx);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, JD9161Z_PG0_GAMMA_SET,
		0x10, 0x04, 0x86, 0x00, 0x1b, 0x35);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, JD9161Z_PG0_DCDC_SEL,
		0x69, 0x0b, 0x30, 0xb2, 0xb2, 0xc0, 0xe0, 0x20,
		0xf0, 0x50, 0x60);

	mipi_dsi_msleep(&dsi_ctx, 1);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, JD9161Z_PG0_VDDD_CTRL,
		0x73, 0x14);

	mipi_dsi_msleep(&dsi_ctx, 1);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, JD9161Z_PG0_SETRGBCYC,
		0x74, 0x04, 0x08, 0x0e, 0x00, 0x0e, 0x0c, 0x08,
		0x0e, 0x00, 0x0e, 0x82, 0x0a, 0x82);

	// param[0][5:4] + param[1]: number of panel lines / 2
	//   400 = 01 1001 0000 -> 0x10, 0x90
	// param[2]: scan line time width
	// param[3]: vfp: 14
	// param[4]: vs + vbp - 1: 11
	// param[5]: hbp: 4
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, JD9161Z_PG0_SET_TCON,
		0x10, 0x90, 0x92, 0x0e, 0x0b, 0x04);

	mipi_dsi_msleep(&dsi_ctx, 1);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, JD9161Z_PG0_SET_R_GAMMA,
		0x7e, 0x76, 0x68, 0x57, 0x4c, 0x39, 0x3a, 0x23,
		0x3d, 0x3d, 0x40, 0x61, 0x54, 0x64, 0x5d, 0x62,
		0x5a, 0x50, 0x32, 0x7e, 0x76, 0x68, 0x57, 0x4c,
		0x39, 0x3a, 0x23, 0x3d, 0x3d, 0x40, 0x61, 0x54,
		0x64, 0x5d, 0x62, 0x5a, 0x50, 0x32);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, JD9161Z_PG0_SET_GIP_L,
		0x1f, 0x0a, 0x08, 0x06, 0x04, 0x1f, 0x00, 0x1f,
		0x17, 0x1f, 0x18, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, JD9161Z_PG0_SET_GIP_R,
		0x1f, 0x0b, 0x09, 0x07, 0x05, 0x1f, 0x01, 0x1f,
		0x17, 0x1f, 0x18, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, JD9161Z_PG0_SETGIP1,
		0x10, 0x00, 0x00, 0x03, 0x60, 0x05, 0x10, 0x00,
		0x02, 0x06, 0x68, 0x00, 0x6c, 0x00, 0x00, 0x00,
		0x00, 0x06, 0x78, 0x71, 0x07, 0x06, 0x68, 0x0c,
		0x25, 0x00, 0x63, 0x03, 0x00);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, JD9161Z_PG0_SETGIP2,
		0x20, 0x10, 0x8c, 0x18, 0x00, 0x80, 0x00, 0x08,
		0x00, 0x00, 0x06, 0x60, 0x00, 0x81, 0x70, 0x02,
		0x30, 0x01, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00,
		0x03, 0x60, 0x83, 0x90, 0x00, 0x00, 0x03, 0x4f,
		0x03, 0x00, 0x1f, 0x3f, 0x00, 0x00, 0x00, 0x00);

	jd9161z_switch_page(&dsi_ctx, 0x04);

	mipi_dsi_msleep(&dsi_ctx, 1);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, JD9161Z_PG4_UNKNOWN_B0,
		0x24, 0x01);

	mipi_dsi_msleep(&dsi_ctx, 1);

	jd9161z_switch_page(&dsi_ctx, 0x02);

	mipi_dsi_msleep(&dsi_ctx, 1);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, JD9161Z_PG2_SETRGBCYC2,
		0x71);

	mipi_dsi_msleep(&dsi_ctx, 1);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, JD9161Z_PG2_UNKNOWN_C2,
		0x00, 0x18, 0x08, 0x1e, 0x25, 0x7c, 0xc7);

	mipi_dsi_msleep(&dsi_ctx, 1);

	jd9161z_switch_page(&dsi_ctx, 0x00);

	mipi_dsi_msleep(&dsi_ctx, 1);

	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);

	mipi_dsi_msleep(&dsi_ctx, 1);

	return dsi_ctx.accum_err;
};

static const struct jd9161z_panel_desc zhunyi_z40046_ctc_desc = {
	.mode = {
		.clock		= (480 + 80 + 80 + 20) * (800 + 20 + 20 + 20) * 60 / 1000,

		.hdisplay	= 480,
		.hsync_start	= 480 + 80,
		.hsync_end	= 480 + 80 + 80,
		.htotal		= 480 + 80 + 80 + 20,

		.vdisplay	= 800,
		.vsync_start	= 800 + 20,
		.vsync_end	= 800 + 20 + 20,
		.vtotal		= 800 + 20 + 20 + 20,

		.width_mm	= 52,
		.height_mm	= 86,
		.flags		= DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
		.type		= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	},
	.lanes = 2,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
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
	struct jd9161z *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct jd9161z, base, &jd9161z_funcs,
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

	ctx->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(ctx->vdd))
		return dev_err_probe(&dsi->dev, PTR_ERR(ctx->vdd),
				     "failed to get vdd regulator\n");

	ctx->vccio = devm_regulator_get(dev, "vccio");
	if (IS_ERR(ctx->vccio))
		return dev_err_probe(&dsi->dev, PTR_ERR(ctx->vccio),
				     "failed to get vccio regulator\n");

	ret = of_drm_get_panel_orientation(dev->of_node, &ctx->orientation);
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

static void jd9161z_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct jd9161z *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->base);
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
