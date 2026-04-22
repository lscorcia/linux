// SPDX-License-Identifier: GPL-2.0
//
// mt8167-afe-clk.c  --  Mediatek 8167 afe clock ctrl
//
// Copyright (c) 2018 MediaTek Inc.
// Author: KaiChieh Chuang <kaichieh.chuang@mediatek.com>

#include <linux/clk.h>

#include "mt8167-afe-common.h"
#include "mt8167-afe-clk.h"

enum {
	//CLK_INFRA_SYS_AUD,
	//CLK_INFRA_SYS_AUD_26M,
	CLK_AUDIO,
	CLK_AUD_AFE,
	//CLK_TOP_MUX_AUD_BUS,
	//CLK_TOP_SYSPLL3_D4,
	//CLK_TOP_SYSPLL1_D4,
	CLK_CLK26M,
	CLK_NUM
};

static const char *aud_clks[CLK_NUM] = {
	//[CLK_INFRA_SYS_AUD] = "infra_sys_audio_clk",
	//[CLK_INFRA_SYS_AUD_26M] = "infra_sys_audio_26m",
	[CLK_AUDIO] = "audio",
	[CLK_AUD_AFE] = "aud_afe",
	//[CLK_TOP_MUX_AUD_BUS] = "top_mux_aud_intbus",
	//[CLK_TOP_SYSPLL3_D4] = "top_sys_pll3_d4",
	//[CLK_TOP_SYSPLL1_D4] = "top_sys_pll1_d4",
	[CLK_CLK26M] = "clk26m",
};

int mt8167_init_clock(struct mtk_base_afe *afe)
{
	struct mt8167_afe_private *afe_priv = afe->platform_priv;
	int i;

	afe_priv->clk = devm_kcalloc(afe->dev, CLK_NUM, sizeof(*afe_priv->clk),
				     GFP_KERNEL);
	if (!afe_priv->clk)
		return -ENOMEM;

	for (i = 0; i < CLK_NUM; i++) {
		afe_priv->clk[i] = devm_clk_get(afe->dev, aud_clks[i]);
		if (IS_ERR(afe_priv->clk[i])) {
			dev_err(afe->dev, "%s(), devm_clk_get %s fail, ret %ld\n",
				__func__, aud_clks[i],
				PTR_ERR(afe_priv->clk[i]));
			return PTR_ERR(afe_priv->clk[i]);
		}
	}

	return 0;
}

int mt8167_afe_enable_clock(struct mtk_base_afe *afe)
{
	struct mt8167_afe_private *afe_priv = afe->platform_priv;
	int ret;
/*
	ret = clk_prepare_enable(afe_priv->clk[CLK_INFRA_SYS_AUD]);
	if (ret) {
		dev_err(afe->dev, "%s(), clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_INFRA_SYS_AUD], ret);
		goto CLK_INFRA_SYS_AUDIO_ERR;
	}

	ret = clk_prepare_enable(afe_priv->clk[CLK_INFRA_SYS_AUD_26M]);
	if (ret) {
		dev_err(afe->dev, "%s(), clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_INFRA_SYS_AUD_26M], ret);
		goto CLK_INFRA_SYS_AUD_26M_ERR;
	}
*/
	ret = clk_prepare_enable(afe_priv->clk[CLK_AUDIO]);
	if (ret) {
		dev_err(afe->dev, "%s(), clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_AUDIO], ret);
		goto CLK_AUDIO_ERR;
	}

	ret = clk_set_parent(afe_priv->clk[CLK_AUDIO],
			     afe_priv->clk[CLK_CLK26M]);
	if (ret) {
		dev_err(afe->dev, "%s(), clk_set_parent %s-%s fail %d\n",
			__func__, aud_clks[CLK_AUDIO],
			aud_clks[CLK_CLK26M], ret);
		goto CLK_AUDIO_ERR;
	}

	ret = clk_prepare_enable(afe_priv->clk[CLK_AUD_AFE]);
	if (ret) {
		dev_err(afe->dev, "%s(), clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_AUD_AFE], ret);
		goto CLK_AUD_AFE_ERR;
	}

	return ret;

CLK_AUD_AFE_ERR:
	clk_disable_unprepare(afe_priv->clk[CLK_AUD_AFE]);
CLK_AUDIO_ERR:
	clk_disable_unprepare(afe_priv->clk[CLK_AUDIO]);
/*CLK_INFRA_SYS_AUD_26M_ERR:
	clk_disable_unprepare(afe_priv->clk[CLK_INFRA_SYS_AUD_26M]);
CLK_INFRA_SYS_AUDIO_ERR:
	clk_disable_unprepare(afe_priv->clk[CLK_INFRA_SYS_AUD]);
*/
	return ret;
}

int mt8167_afe_disable_clock(struct mtk_base_afe *afe)
{
	struct mt8167_afe_private *afe_priv = afe->platform_priv;

	clk_disable_unprepare(afe_priv->clk[CLK_AUD_AFE]);
	clk_disable_unprepare(afe_priv->clk[CLK_AUDIO]);
//	clk_disable_unprepare(afe_priv->clk[CLK_INFRA_SYS_AUD_26M]);
//	clk_disable_unprepare(afe_priv->clk[CLK_INFRA_SYS_AUD]);

	return 0;
}
