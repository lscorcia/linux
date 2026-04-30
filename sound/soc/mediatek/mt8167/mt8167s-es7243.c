// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek MT8167 Sound Card driver
 *
 * Copyright (c) 2024 MediaTek Inc.
 * Authors: Nicolas Belin <nbelin@baylibre.com>
 */

#include <linux/array_size.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <sound/soc.h>
#include <sound/pcm_params.h>

#include "mt8167-afe-common.h"
#include "../common/mtk-soc-card.h"
#include "../common/mtk-soundcard-driver.h"

enum pinctrl_pin_state {
	PIN_STATE_DEFAULT = 0,
	PIN_STATE_EXTAMP_ON,
	PIN_STATE_EXTAMP_OFF,
	PIN_STATE_MAX
};

struct mt8167_evb_priv {
	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_states[PIN_STATE_MAX];
	struct regulator *tdmadc_1p8_supply;
	struct regulator *tdmadc_3p3_supply;
	uint32_t ext_spk_amp_warmup_time_us;
	uint32_t ext_spk_amp_shutdown_time_us;
	uint32_t hp_spk_amp_warmup_time_us;
	uint32_t hp_spk_amp_shutdown_time_us;
	long ext_spk_amp_en;
	long ext_hp_amp_en;
};

static const char * const mt8167_evb_pin_str[PIN_STATE_MAX] = {
	"default",
	"extamp_on",
	"extamp_off",
};

static void mt8167_evb_ext_spk_amp_turn_on(struct snd_soc_card *card)
{
	struct mt8167_evb_priv *card_data = snd_soc_card_get_drvdata(card);
	int ret;

	if (IS_ERR(card_data->pin_states[PIN_STATE_EXTAMP_ON]))
		return;

	ret = pinctrl_select_state(card_data->pinctrl,
		card_data->pin_states[PIN_STATE_EXTAMP_ON]);
	if (ret)
		dev_err(card->dev, "%s failed to select state %d\n",
			__func__, ret);

	if (card_data->ext_spk_amp_warmup_time_us > 0)
		usleep_range(card_data->ext_spk_amp_warmup_time_us,
			card_data->ext_spk_amp_warmup_time_us + 1);
}

static void mt8167_evb_ext_spk_amp_turn_off(struct snd_soc_card *card)
{
	struct mt8167_evb_priv *card_data = snd_soc_card_get_drvdata(card);
	int ret;

	if (IS_ERR(card_data->pin_states[PIN_STATE_EXTAMP_OFF]))
		return;

	ret = pinctrl_select_state(card_data->pinctrl,
		card_data->pin_states[PIN_STATE_EXTAMP_OFF]);
	if (ret)
		dev_err(card->dev, "%s failed to select state %d\n",
			__func__, ret);

	if (card_data->ext_spk_amp_shutdown_time_us > 0)
		usleep_range(card_data->ext_spk_amp_shutdown_time_us,
			card_data->ext_spk_amp_shutdown_time_us + 1);
}

/* Ext Spk Amp Switch */
static int mt8167_evb_ext_spk_amp_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct mt8167_evb_priv *card_data = snd_soc_card_get_drvdata(card);

	ucontrol->value.integer.value[0] = card_data->ext_spk_amp_en;

	return 0;
}

static int mt8167_evb_ext_spk_amp_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct mt8167_evb_priv *card_data = snd_soc_card_get_drvdata(card);

	card_data->ext_spk_amp_en = ucontrol->value.integer.value[0];

	if (card_data->ext_spk_amp_en)
		mt8167_evb_ext_spk_amp_turn_on(card);
	else
		mt8167_evb_ext_spk_amp_turn_off(card);

	return 0;
}

static void mt8167_evb_ext_hp_amp_turn_on(struct snd_soc_card *card)
{
	struct mt8167_evb_priv *card_data = snd_soc_card_get_drvdata(card);
	int ret;

	if (IS_ERR(card_data->pin_states[PIN_STATE_EXTAMP_ON]))
		return;

	ret = pinctrl_select_state(card_data->pinctrl,
		card_data->pin_states[PIN_STATE_EXTAMP_ON]);
	if (ret)
		dev_err(card->dev, "%s failed to select state %d\n",
			__func__, ret);

	if (card_data->hp_spk_amp_warmup_time_us > 0)
		usleep_range(card_data->hp_spk_amp_warmup_time_us,
			card_data->hp_spk_amp_warmup_time_us + 1);
}

static void mt8167_evb_ext_hp_amp_turn_off(struct snd_soc_card *card)
{
	struct mt8167_evb_priv *card_data = snd_soc_card_get_drvdata(card);
	int ret;

	if (IS_ERR(card_data->pin_states[PIN_STATE_EXTAMP_OFF]))
		return;

	ret = pinctrl_select_state(card_data->pinctrl,
		card_data->pin_states[PIN_STATE_EXTAMP_OFF]);
	if (ret)
		dev_err(card->dev, "%s failed to select state %d\n",
			__func__, ret);

	if (card_data->hp_spk_amp_shutdown_time_us > 0)
		usleep_range(card_data->hp_spk_amp_shutdown_time_us,
			card_data->hp_spk_amp_shutdown_time_us + 1);
}

/* Ext HP Amp Switch */
static int mt8167_evb_ext_hp_amp_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct mt8167_evb_priv *card_data = snd_soc_card_get_drvdata(card);

	ucontrol->value.integer.value[0] = card_data->ext_hp_amp_en;

	return 0;
}

static int mt8167_evb_ext_hp_amp_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct mt8167_evb_priv *card_data = snd_soc_card_get_drvdata(card);

	card_data->ext_hp_amp_en = ucontrol->value.integer.value[0];

	if (card_data->ext_hp_amp_en)
		mt8167_evb_ext_hp_amp_turn_on(card);
	else
		mt8167_evb_ext_hp_amp_turn_off(card);

	return 0;
}

static const struct snd_kcontrol_new mt8167_evb_controls[] = {
	/* Ext Spk Amp Switch */
	SOC_SINGLE_BOOL_EXT("Ext Spk Amp Switch",
		0,
		mt8167_evb_ext_spk_amp_get,
		mt8167_evb_ext_spk_amp_put),
	/* Ext HP Amp Switch */
	SOC_SINGLE_BOOL_EXT("Ext HP Amp Switch",
		0,
		mt8167_evb_ext_hp_amp_get,
		mt8167_evb_ext_hp_amp_put),
};

// Widgets

static int mt8167_evb_mic1_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *codec = snd_soc_dapm_to_component(w->dapm);

	dev_dbg(codec->dev, "%s, event %d\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		break;
	case SND_SOC_DAPM_POST_PMD:
		break;
	default:
		break;
	}

	return 0;
}

static int mt8167_evb_mic2_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *codec = snd_soc_dapm_to_component(w->dapm);

	dev_dbg(codec->dev, "%s, event %d\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		break;
	case SND_SOC_DAPM_POST_PMD:
		break;
	default:
		break;
	}

	return 0;
}

static int mt8167_evb_headset_mic_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *codec = snd_soc_dapm_to_component(w->dapm);

	dev_dbg(codec->dev, "%s, event %d\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		break;
	case SND_SOC_DAPM_POST_PMD:
		break;
	default:
		break;
	}

	return 0;
}

/* HP Ext Amp Switch */
static const struct snd_kcontrol_new mt8167_evb_hp_ext_amp_switch_ctrl =
	SOC_DAPM_SINGLE_VIRT("Switch", 1);

/* HP Spk Amp */
static int mt8167_evb_hp_spk_amp_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *codec = snd_soc_dapm_to_component(w->dapm);
	dev_dbg(codec->dev, "%s, event %d\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		mt8167_evb_ext_hp_amp_turn_on(codec->card);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		mt8167_evb_ext_hp_amp_turn_off(codec->card);
		break;
	default:
		break;
	}

	return 0;
}

/* LINEOUT Ext Amp Switch */
static const struct snd_kcontrol_new mt8167_evb_lineout_ext_amp_switch_ctrl =
	SOC_DAPM_SINGLE_VIRT("Switch", 1);

/* Ext Spk Amp */
static int mt8167_evb_ext_spk_amp_wevent(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *codec = snd_soc_dapm_to_component(w->dapm);
	struct mt8167_evb_priv *card_data = snd_soc_component_get_drvdata(codec);
	int ret = 0;

	dev_dbg(codec->dev, "%s, event %d\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (!IS_ERR(card_data->pin_states[PIN_STATE_EXTAMP_ON])) {
			ret = pinctrl_select_state(
				card_data->pinctrl,
				card_data->pin_states[PIN_STATE_EXTAMP_ON]);
			if (ret)
				dev_err(codec->dev, "%s failed to select state %d\n",
					__func__, ret);
		}
		break;
	case SND_SOC_DAPM_PRE_PMD:
		if (!IS_ERR(card_data->pin_states[PIN_STATE_EXTAMP_OFF])) {
			ret = pinctrl_select_state(
				card_data->pinctrl,
				card_data->pin_states[PIN_STATE_EXTAMP_OFF]);
			if (ret)
				dev_err(codec->dev, "%s failed to select state %d\n",
					__func__, ret);
		}
		break;
	default:
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget mt8167_evb_widgets[] = {
	SND_SOC_DAPM_MIC("Mic 1", mt8167_evb_mic1_event),
	SND_SOC_DAPM_MIC("Mic 2", mt8167_evb_mic2_event),
	SND_SOC_DAPM_MIC("Headset Mic", mt8167_evb_headset_mic_event),
	SND_SOC_DAPM_SPK("HP Spk Amp", mt8167_evb_hp_spk_amp_event),
	SND_SOC_DAPM_SWITCH("HP Ext Amp",
		SND_SOC_NOPM, 0, 0, &mt8167_evb_hp_ext_amp_switch_ctrl),
	SND_SOC_DAPM_SPK("Ext Spk Amp", mt8167_evb_ext_spk_amp_wevent),
	SND_SOC_DAPM_SWITCH("LINEOUT Ext Amp",
		SND_SOC_NOPM, 0, 0, &mt8167_evb_lineout_ext_amp_switch_ctrl),
};

// Routes

static const struct snd_soc_dapm_route mt8167_evb_routes[] = {
	/* Uplink */

	{"AU_VIN0", NULL, "Mic 1"},
	{"AU_VIN2", NULL, "Mic 2"},

	{"AU_VIN1", NULL, "Headset Mic"},

	/* Downlink */

	/* use external spk amp via AU_HPL/AU_HPR */
	{"HP Ext Amp", "Switch", "AU_HPL"},
	{"HP Ext Amp", "Switch", "AU_HPR"},

	{"HP Spk Amp", NULL, "HP Ext Amp"},
	{"HP Spk Amp", NULL, "HP Ext Amp"},

	/* use internal spk amp of MT6392 */
	{"MT6392 AIF RX", NULL, "AU_LOL"},

	/* use external spk amp via AU_LOL */
	{"LINEOUT Ext Amp", "Switch", "AU_LOL"},
	{"Ext Spk Amp", NULL, "LINEOUT Ext Amp"},

	/* ADDA clock - Uplink */
	{"AIF TX", NULL, "audio"},
	{"AIF TX", NULL, "aud_adc"},

	/* ADDA clock - Downlink */
	{"AIF RX", NULL, "audio"},
	{"AIF RX", NULL, "aud_dac"},
};

enum {
	/* FE */
	DAI_LINK_DL1_PLAYBACK = 0,
	DAI_LINK_VUL_CAPTURE,
	//DAI_LINK_HDMI_PLAYBACK,
	//DAI_LINK_AWB_CAPTURE,
	DAI_LINK_DL2_PLAYBACK,
	//DAI_LINK_DAI_CAPTURE,
	//DAI_LINK_TDM_CAPTURE,
	//DAI_LINK_VIRTUAL_MRG,
	//DAI_LINK_MRGRX_CAPTURE,
	//DAI_LINK_BTCVSD_RX,
	//DAI_LINK_BTCVSD_TX,
	/* BE */
	//DAI_LINK_I2S_INTF,
	//DAI_LINK_2ND_I2S_INTF,
	DAI_LINK_INT_ADDA,
	//DAI_LINK_HDMI_BE,
	//DAI_LINK_DL_BE,
	//DAI_LINK_MRG_BT_BE,
	//DAI_LINK_PCM0_BE,
	//DAI_LINK_TDM_IN_BE,
	DAI_LINK_NUM
};

// FE
SND_SOC_DAILINK_DEFS(playback1,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(vul,
		     DAILINK_COMP_ARRAY(COMP_CPU("VUL")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(hdmi,
		     DAILINK_COMP_ARRAY(COMP_CPU("HDMI")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(awb_capture,
		     DAILINK_COMP_ARRAY(COMP_CPU("AWB")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(playback2,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL2")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(dai,
		     DAILINK_COMP_ARRAY(COMP_CPU("DAI")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(tdm_in,
		     DAILINK_COMP_ARRAY(COMP_CPU("TDM_IN")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(virtual_mrg,
		     DAILINK_COMP_ARRAY(COMP_CPU("VIRTURL_MRG")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

// BE
SND_SOC_DAILINK_DEFS(i2s1,
		     DAILINK_COMP_ARRAY(COMP_CPU("I2S")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(i2s2,
		     DAILINK_COMP_ARRAY(COMP_CPU("2ND I2S")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(primary_codec,
		     DAILINK_COMP_ARRAY(COMP_CPU("INT ADDA")),
		     DAILINK_COMP_ARRAY(COMP_CODEC("mt8167-codec", "mt8167-codec-dai")),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(hdmi_be,
		     DAILINK_COMP_ARRAY(COMP_CPU("HDMIO")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(dl_input,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL Input")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(mrg_bt,
		     DAILINK_COMP_ARRAY(COMP_CPU("MRG BT")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(pcm0,
		     DAILINK_COMP_ARRAY(COMP_CPU("PCM0")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(tdm_in_io,
		     DAILINK_COMP_ARRAY(COMP_CPU("TDM_IN_IO")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

/* Digital audio interface glue - connects codec <---> CPU */
static struct snd_soc_dai_link mt8167_evb_dais[] = {
	/* Front End DAI links */
	[DAI_LINK_DL1_PLAYBACK] = {
		.name = "DL1 Playback",
		.stream_name = "MultiMedia1_PLayback",
		.id = DAI_LINK_DL1_PLAYBACK,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.playback_only = 1,
		//.dpcm_merged_rate = 1,
		SND_SOC_DAILINK_REG(playback1),
	},
	[DAI_LINK_VUL_CAPTURE] = {
		.name = "VUL Capture",
		.stream_name = "MultiMedia1_Capture",
		.id = DAI_LINK_VUL_CAPTURE,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.capture_only = 1,
		//.dpcm_merged_rate = 1,
		SND_SOC_DAILINK_REG(vul),
	},/*
	[DAI_LINK_HDMI_PLAYBACK] = {
		.name = "HDMI",
		.stream_name = "HMDI_PLayback",
		.id = DAI_LINK_HDMI_PLAYBACK,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.capture_only = 1,
		//.dpcm_merged_rate = 1,
		SND_SOC_DAILINK_REG(hdmi),
	},
	[DAI_LINK_AWB_CAPTURE] = {
		.name = "AWB Capture",
		.stream_name = "DL1_AWB_Record",
		.id = DAI_LINK_AWB_CAPTURE,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.capture_only = 1,
		//.dpcm_merged_rate = 1,
		SND_SOC_DAILINK_REG(awb_capture),
	},*/
	[DAI_LINK_DL2_PLAYBACK] = {
		.name = "DL2 Playback",
		.stream_name = "MultiMedia2_PLayback",
		.id = DAI_LINK_DL2_PLAYBACK,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.playback_only = 1,
		//.dpcm_merged_rate = 1,
		SND_SOC_DAILINK_REG(playback2),
	},/*
	[DAI_LINK_DAI_CAPTURE] = {
		.name = "DAI Capture",
		.stream_name = "VOIP_Call_BT_Capture",
		.id = DAI_LINK_DAI_CAPTURE,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.capture_only = 1,
		//.dpcm_merged_rate = 1,
		SND_SOC_DAILINK_REG(dai),
	},
	[DAI_LINK_TDM_CAPTURE] = {
		.name = "TDM Capture",
		.stream_name = "TDM_Capture",
		.id = DAI_LINK_TDM_CAPTURE,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.capture_only = 1,
		//.dpcm_merged_rate = 1,
		SND_SOC_DAILINK_REG(tdm_in),
	},
	[DAI_LINK_VIRTUAL_MRG] = {
		.name = "VIRTUAL_MRG",
		.stream_name = "MRGRX_PLayback",
		.id = DAI_LINK_VIRTUAL_MRG,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.playback_only = 1,
		//.dpcm_merged_rate = 1,
		SND_SOC_DAILINK_REG(virtual_mrg),
	},
	[DAI_LINK_MRGRX_CAPTURE] = {
		.name = "MRGRX_CAPTURE",
		.stream_name = "MRGRX_CAPTURE",
		.id = DAI_LINK_TDM_CAPTURE,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.capture_only = 1,
		//.dpcm_merged_rate = 1,
		SND_SOC_DAILINK_REG(awb_capture),
	},*/
	/* Back End DAI links */
	/*[DAI_LINK_I2S_INTF] = {
		.name = "EXT Codec",
		.no_pcm = 1,
		.id = DAI_LINK_I2S_INTF,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBC_CFC,
		SND_SOC_DAILINK_REG(i2s1),
	},
	[DAI_LINK_2ND_I2S_INTF] = {
		.name = "2ND EXT Codec",
		.no_pcm = 1,
		.id = DAI_LINK_2ND_I2S_INTF,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBC_CFC,
		SND_SOC_DAILINK_REG(i2s2),
	},*/
	[DAI_LINK_INT_ADDA] = {
		.name = "MTK Codec",
		.no_pcm = 1,
		.id = DAI_LINK_INT_ADDA,
		SND_SOC_DAILINK_REG(primary_codec),
	},/*
	[DAI_LINK_HDMI_BE] = {
		.name = "HDMI BE",
		.no_pcm = 1,
		.id = DAI_LINK_HDMI_BE,
		SND_SOC_DAILINK_REG(hdmi_be),
	},
	[DAI_LINK_DL_BE] = {
		.name = "DL BE",
		.no_pcm = 1,
		.id = DAI_LINK_DL_BE,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(dl_input),
	},
	[DAI_LINK_MRG_BT_BE] = {
		.name = "MRG BT BE",
		.no_pcm = 1,
		.id = DAI_LINK_MRG_BT_BE,
		SND_SOC_DAILINK_REG(mrg_bt),
	},
	[DAI_LINK_PCM0_BE] = {
		.name = "PCM0 BE",
		.no_pcm = 1,
		.id = DAI_LINK_PCM0_BE,
		SND_SOC_DAILINK_REG(pcm0),
	},
	[DAI_LINK_TDM_IN_BE] = {
		.name = "TDM IN BE",
		.no_pcm = 1,
		.id = DAI_LINK_TDM_IN_BE,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBC_CFC,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(tdm_in_io),
	},*/
};

static void tdmadc_supply_disable(void *r)
{
	regulator_disable((struct regulator *) r);
}

static int mt8167_evb_suspend_post(struct snd_soc_card *card)
{
	struct mt8167_evb_priv *card_data;

	card_data = snd_soc_card_get_drvdata(card);

	if (!IS_ERR(card_data->tdmadc_1p8_supply))
		regulator_disable(card_data->tdmadc_1p8_supply);
	if (!IS_ERR(card_data->tdmadc_3p3_supply))
		regulator_disable(card_data->tdmadc_3p3_supply);
	return 0;
}

static int mt8167_evb_resume_pre(struct snd_soc_card *card)
{
	struct mt8167_evb_priv *card_data;
	int ret;

	card_data = snd_soc_card_get_drvdata(card);

	/* tdm adc power down */
	if (!IS_ERR(card_data->tdmadc_1p8_supply)) {
		ret = regulator_enable(card_data->tdmadc_1p8_supply);
		if (ret != 0)
			dev_err(card->dev,
				"%s failed to enable tdm 1p8 supply %d!\n",
				__func__, ret);
	}
	if (!IS_ERR(card_data->tdmadc_3p3_supply)) {
		ret = regulator_enable(card_data->tdmadc_3p3_supply);
		if (ret != 0)
			dev_err(card->dev,
				"%s failed to enable tdm 3p3 supply %d!\n",
				__func__, ret);
	}
	return 0;
}

static struct snd_soc_aux_dev mt8167_evb_aux_dev[] = {
	{
		.dlc = COMP_CODEC("mt6392-codec", "mt6392-codec-dai"),
	},
};

static struct snd_soc_card mt8167_evb_soc_card = {
	.name = "mt8167s-es7243",
	.owner = THIS_MODULE,
	.dai_link = mt8167_evb_dais,
	.num_links = ARRAY_SIZE(mt8167_evb_dais),
	.controls = mt8167_evb_controls,
	.num_controls = ARRAY_SIZE(mt8167_evb_controls),
	.dapm_widgets = mt8167_evb_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt8167_evb_widgets),
	.dapm_routes = mt8167_evb_routes,
	.num_dapm_routes = ARRAY_SIZE(mt8167_evb_routes),
	.suspend_post = mt8167_evb_suspend_post,
	.resume_pre = mt8167_evb_resume_pre,
	.aux_dev = mt8167_evb_aux_dev,
	.num_aux_devs = ARRAY_SIZE(mt8167_evb_aux_dev),
};

static int mt8167_evb_regulator_probe(struct snd_soc_card *card)
{
	struct mt8167_evb_priv *card_data;
	int isenable, vol, ret;

	pr_notice("mt8167_evb_regulator_probe!!\r\n");

	card_data = snd_soc_card_get_drvdata(card);

	card_data->tdmadc_3p3_supply =
		devm_regulator_get(card->dev, "tdmadc-3p3v");
	if (IS_ERR(card_data->tdmadc_3p3_supply)) {
		ret = PTR_ERR(card_data->tdmadc_3p3_supply);
		dev_err(card->dev,
			"%s failed to get tdmadc-3p3v regulator %d\n", __func__,
			ret);
		return ret;
	}

	ret = regulator_set_voltage(card_data->tdmadc_3p3_supply, 3300000,
				    3300000);
	if (ret != 0) {
		dev_err(card->dev,
			"%s failed to set tdmadc-3p3v supply to 3.3v %d\n",
			__func__, ret);
		return ret;
	}

	ret = regulator_enable(card_data->tdmadc_3p3_supply);
	if (ret != 0) {
		dev_err(card->dev,
			"%s failed to enable tdmadc 3p3 supply %d!\n", __func__,
			ret);
		return ret;
	}

	devm_add_action_or_reset(card->dev, tdmadc_supply_disable,
				 card_data->tdmadc_3p3_supply);

	isenable = regulator_is_enabled(card_data->tdmadc_3p3_supply);
	if (isenable != 1)
		dev_err(card->dev, "%s tdmadc 3.3V supply is not enabled\n",
			__func__);

	vol = regulator_get_voltage(card_data->tdmadc_3p3_supply);
	if (vol != 3300000)
		dev_err(card->dev, "%s tdmadc 3p3 supply != 3.3V (%d)\n",
			__func__, vol);

	card_data->tdmadc_1p8_supply =
		devm_regulator_get(card->dev, "tdmadc-1p8v");
	if (IS_ERR(card_data->tdmadc_1p8_supply)) {
		ret = PTR_ERR(card_data->tdmadc_1p8_supply);
		dev_err(card->dev,
			"%s failed to get tdmadc-1p8v regulator %d\n", __func__,
			ret);
		return ret;
	}

	ret = regulator_set_voltage(card_data->tdmadc_1p8_supply, 1800000,
				    1800000);
	if (ret != 0) {
		dev_err(card->dev,
			"%s failed to set tdmadc-1p8v supply to 1.8v %d\n",
			__func__, ret);
		return ret;
	}

	ret = regulator_enable(card_data->tdmadc_1p8_supply);
	if (ret != 0) {
		dev_err(card->dev,
			"%s failed to enable tdmadc 1p8 supply %d!\n", __func__,
			ret);
		return ret;
	}

	devm_add_action_or_reset(card->dev, tdmadc_supply_disable,
				 card_data->tdmadc_1p8_supply);

	isenable = regulator_is_enabled(card_data->tdmadc_1p8_supply);
	if (isenable != 1)
		dev_err(card->dev, "%s tdmadc 1.8V supply is not enabled\n",
			__func__);

	vol = regulator_get_voltage(card_data->tdmadc_1p8_supply);
	if (vol != 1800000)
		dev_err(card->dev, "%s tdmadc 1p8 supply != 1.8V (%d)\n",
			__func__, vol);

	return 0;
}

static int mt8167_evb_gpio_probe(struct snd_soc_card *card)
{
	struct mt8167_evb_priv *priv = snd_soc_card_get_drvdata(card);
	int ret, i;

	priv->pinctrl = devm_pinctrl_get(card->dev);
	if (IS_ERR(priv->pinctrl)) {
		ret = PTR_ERR(priv->pinctrl);
		return dev_err_probe(card->dev, ret,
				     "Failed to get pinctrl\n");
	}

	for (i = PIN_STATE_DEFAULT ; i < PIN_STATE_MAX ; i++) {
		priv->pin_states[i] = pinctrl_lookup_state(priv->pinctrl,
							   mt8167_evb_pin_str[i]);
		if (IS_ERR(priv->pin_states[i])) {
			dev_info(card->dev, "No pin state for %s\n",
				 mt8167_evb_pin_str[i]);
		} else {
			ret = pinctrl_select_state(priv->pinctrl,
						   priv->pin_states[i]);
			if (ret) {
				dev_err_probe(card->dev, ret,
					      "Failed to select pin state %s\n",
					      mt8167_evb_pin_str[i]);
				return ret;
			}
		}
	}

	return 0;
}

static int mt8167_evb_dev_probe(struct mtk_soc_card_data *soc_card_data, bool legacy)
{
	struct mtk_platform_card_data *card_data = soc_card_data->card_data;
	struct snd_soc_card *card = card_data->card;
	struct device *dev = card->dev;
	struct mt8167_evb_priv *mach_priv;
	int ret;

	card->dev = dev;
	ret = parse_dai_link_info(card);
	if (ret)
		goto err;

	mach_priv = devm_kzalloc(dev, sizeof(*mach_priv),
				 GFP_KERNEL);
	if (!mach_priv)
		return -ENOMEM;

	soc_card_data->mach_priv = mach_priv;

	snd_soc_card_set_drvdata(card, soc_card_data);

	ret = mt8167_evb_regulator_probe(card);
	if (ret)
		dev_err(dev, "%s mt8167_evb_regulator_probe fail\n", __func__);

	ret = mt8167_evb_gpio_probe(card);
	if (ret)
		dev_err(dev, "%s mt8167_evb_gpio_probe fail\n", __func__);

	of_property_read_u32(dev->of_node,
			"mediatek,ext-spk-amp-warmup-time-us",
			&mach_priv->ext_spk_amp_warmup_time_us);

	of_property_read_u32(dev->of_node,
			"mediatek,ext-spk-amp-shutdown-time-us",
			&mach_priv->ext_spk_amp_shutdown_time_us);

	of_property_read_u32(dev->of_node,
			 "mediatek,hp-spk-amp-warmup-time-us",
			 &mach_priv->hp_spk_amp_warmup_time_us);

	of_property_read_u32(dev->of_node,
			 "mediatek,hp-spk-amp-shutdown-time-us",
			 &mach_priv->hp_spk_amp_shutdown_time_us);

	return 0;

err:
	clean_card_reference(card);
	return ret;
}

static const struct mtk_soundcard_pdata mt8167_evb_card = {
	.card_name = "mt8167s-es7243",
	.card_data = &(struct mtk_platform_card_data) {
		.card = &mt8167_evb_soc_card,
	},
	.soc_probe = mt8167_evb_dev_probe
};

static const struct of_device_id mt8167_evb_dt_match[] = {
	{ .compatible = "mediatek,mt8167s-es7243", .data = &mt8167_evb_card },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mt8167_evb_dt_match);

static struct platform_driver mt8167_evb_driver = {
	.driver = {
		   .name = "mt8167s-es7243",
		   .of_match_table = mt8167_evb_dt_match,
		   .pm = &snd_soc_pm_ops,
	},
	.probe = mtk_soundcard_common_probe,
};

module_platform_driver(mt8167_evb_driver);

/* Module information */
MODULE_DESCRIPTION("MT8167 EVB ALSA SoC machine driver");
MODULE_AUTHOR("Nicolas Belin <nbelin@baylibre.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform: mt8167_evb");
MODULE_SOFTDEP("pre: snd_soc_mt8167_afe");