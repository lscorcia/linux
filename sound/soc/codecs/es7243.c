/*
 * ALSA SoC ES7243 codec driver
 *
 * I2C device on address 0x13
 *
 * Based on the kernel sources for the Realme TechLife Robot Vacuum
 * https://github.com/realme-kernel-opensource/realme_TechLife_Robot_Vacuum-kernel-source/blob/ef379d496bc0cd4b7bbeced783245f5e450a506d/sound/soc/codecs/es7243.c
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#define ES7243_REGISTER_COUNT 0xff

#define ES7243_RATES (SNDRV_PCM_RATE_8000 | \
		       SNDRV_PCM_RATE_11025 | \
		       SNDRV_PCM_RATE_16000 | \
		       SNDRV_PCM_RATE_22050 | \
		       SNDRV_PCM_RATE_32000 | \
		       SNDRV_PCM_RATE_44100 | \
		       SNDRV_PCM_RATE_48000)

#define ES7243_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE | \
	 SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S20_3BE | \
	 SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_BE | \
	 SNDRV_PCM_FMTBIT_S32_LE)

struct reg_default es7243_reg_defaults[ES7243_REGISTER_COUNT] = {
};

static int es7243_first_start;
static struct i2c_client *es7243_client;

struct es7243_priv {
	struct regmap *regmap;
	struct snd_soc_codec *codec;
	struct tas57xx_platform_data *pdata;

	/* Platform provided EQ configuration */
	int num_eq_conf_texts;
	const char **eq_conf_texts;
	int eq_cfg;
	struct soc_enum eq_conf_enum;
	unsigned char Ch1_vol;
	unsigned char Ch2_vol;
	unsigned char master_vol;
	unsigned int mclk;
	unsigned int EQ_enum_value;
	unsigned int DRC_enum_value;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
};

#if 0
static int es7243_set_EQ_enum(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol);
static int es7243_get_EQ_enum(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol);
static int es7243_set_DRC_enum(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol);
static int es7243_get_DRC_enum(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol);

static const DECLARE_TLV_DB_SCALE(mvol_tlv, -12700, 50, 1);
static const DECLARE_TLV_DB_SCALE(chvol_tlv, -10300, 50, 1);

static const struct snd_kcontrol_new es7243_snd_controls[] = {
	SOC_SINGLE_TLV("Master Volume", DDX_MASTER_VOLUME, 0,
			   0xff, 1, mvol_tlv),
	SOC_SINGLE_TLV("Ch1 Volume", DDX_CHANNEL1_VOL, 0,
			   0xff, 1, chvol_tlv),
	SOC_SINGLE_TLV("Ch2 Volume", DDX_CHANNEL2_VOL, 0,
			   0xff, 1, chvol_tlv),
	SOC_SINGLE("Ch1 Switch", DDX_SOFT_MUTE, 0, 1, 1),
	SOC_SINGLE("Ch2 Switch", DDX_SOFT_MUTE, 1, 1, 1),
	SOC_SINGLE_RANGE("Fine Master Volume", DDX_CHANNEL3_VOL, 0,
			   0x80, 0x83, 0),
	SOC_SINGLE_BOOL_EXT("Set EQ Enable", 0,
			   es7243_get_EQ_enum, es7243_set_EQ_enum),
	SOC_SINGLE_BOOL_EXT("Set DRC Enable", 0,
			   es7243_get_DRC_enum, es7243_set_DRC_enum),
};
#endif

static int es7243_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct es7243_priv *es7243 = snd_soc_component_get_drvdata(
		codec_dai->component);
	es7243->mclk = freq;

	return 0;
}

static int es7243_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC:
		break;
	default:
		return 0;//-EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		break;
	default:
		return 0;//-EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_NB_IF:
		break;
	default:
		return 0;//-EINVAL;
	}

	return 0;
}

static int es7243_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	unsigned int rate;

	rate = params_rate(params);
	pr_debug("rate: %u\n", rate);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_BE:
		pr_debug("24bit\n");
	__attribute__((__fallthrough__));
	case SNDRV_PCM_FORMAT_S32_LE:
	case SNDRV_PCM_FORMAT_S20_3LE:
	case SNDRV_PCM_FORMAT_S20_3BE:
		pr_debug("20bit\n");

		break;
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_S16_BE:
		pr_debug("16bit\n");

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int es7243_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *codec_dai)
{
	struct snd_soc_component *codec = codec_dai->component;
	if (es7243_first_start == 0) {
		snd_soc_component_write(codec, 0x00, 0x41);
		snd_soc_component_write(codec, 0x06, 0x18);
		snd_soc_component_write(codec, 0x05, 0x1B);
		snd_soc_component_write(codec, 0x01, 0x0C);
		snd_soc_component_write(codec, 0x04, 0x04);
		snd_soc_component_write(codec, 0x08, 0x11);
		snd_soc_component_write(codec, 0x06, 0x00);
		snd_soc_component_write(codec, 0x05, 0x13);

		es7243_first_start = 1;
	}

	return 0;
}

static const struct snd_soc_dai_ops es7243_dai_ops = {
	.hw_params = es7243_hw_params,
	.set_sysclk = es7243_set_dai_sysclk,
	.set_fmt = es7243_set_dai_fmt,
	.prepare = es7243_prepare,
};

static struct snd_soc_dai_driver es7243_dai = {
	.name = "es7243-codec-dai",
	.playback = {
		.stream_name = "HIFI Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = ES7243_RATES,
		.formats = ES7243_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = ES7243_RATES,
		.formats = ES7243_FORMATS,
	},
	.ops = &es7243_dai_ops,
};

static int es7243_probe(struct snd_soc_component *codec)
{
	snd_soc_component_write(codec, 0x00, 0x41);
	snd_soc_component_write(codec, 0x06, 0x18);
	snd_soc_component_write(codec, 0x05, 0x1B);
	snd_soc_component_write(codec, 0x01, 0x0C);
	snd_soc_component_write(codec, 0x04, 0x04);
	snd_soc_component_write(codec, 0x08, 0x11);
	snd_soc_component_write(codec, 0x06, 0x00);
	snd_soc_component_write(codec, 0x05, 0x13);

	return 0;
}

static void es7243_remove(struct snd_soc_component *codec)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct es7243_priv *es7243 = snd_soc_component_get_drvdata(&codec->component);

	unregister_early_suspend(&(es7243->early_suspend));
#endif
}

static int es7243_suspend(struct snd_soc_component *codec)
{
	return 0;
}

static int es7243_resume(struct snd_soc_component *codec)
{
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void es7243_early_suspend(struct early_suspend *h)
{
}

static void es7243_late_resume(struct early_suspend *h)
{
}
#endif

static const struct snd_soc_component_driver soc_codec_dev_es7243 = {
	.probe = es7243_probe,
	.remove = es7243_remove,
	.suspend = es7243_suspend,
	.resume = es7243_resume,
};

static const struct regmap_config es7243_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = ES7243_REGISTER_COUNT,
	.reg_defaults = es7243_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(es7243_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
};

static int es7243_pd(struct es7243_priv *es7243, struct device_node *np)
{
	return 0;
}

static int es7243_i2c_probe(struct i2c_client *client)
{
	struct es7243_priv *es7243;
	int ret;

	es7243_client = client;
	es7243 = devm_kzalloc(&client->dev, sizeof(struct es7243_priv),
		GFP_KERNEL);
	if (!es7243)
		return -ENOMEM;

	es7243->regmap = devm_regmap_init_i2c(client, &es7243_regmap);
	if (IS_ERR(es7243->regmap)) {
		ret = PTR_ERR(es7243->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	dev_set_name(&client->dev, "%s", "es7243");
	i2c_set_clientdata(client, es7243);
	es7243_pd(es7243, client->dev.of_node);

	ret = snd_soc_register_component(&client->dev, &soc_codec_dev_es7243,
		&es7243_dai, 1);
	if (ret != 0)
		printk(KERN_DEBUG"es7243 i2c probe failed\n");

	return ret;

}

static void es7243_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_component(&client->dev);
}

static const struct i2c_device_id es7243_i2c_id[] = {
	{ "es7243", 0 },
	{}
};

static const struct of_device_id es7243_of_id[] = {
	{.compatible = "mi,es7243",},
	{}
};
MODULE_DEVICE_TABLE(of, es7243_of_id);

static struct  i2c_driver es7243_i2c_driver = {
	.driver = {
		.name = "es7243",
		.of_match_table = es7243_of_id,
		.owner = THIS_MODULE,
	},
	.probe = es7243_i2c_probe,
	.remove = es7243_i2c_remove,
	.id_table = es7243_i2c_id,
};
module_i2c_driver(es7243_i2c_driver);

MODULE_DESCRIPTION("ASoC ES7243 driver");
MODULE_LICENSE("GPL");
