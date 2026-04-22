/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 BayLibre, SAS
 * Author: Fabien Parent <fparent@baylibre.com>
 */

#ifndef _MT8167_AFE_COMMON_H_
#define _MT8167_AFE_COMMON_H_

#include <sound/soc.h>
#include <linux/list.h>
#include <linux/regmap.h>
#include "../common/mtk-base-afe.h"

struct clk;

struct mt8167_afe_private {
	struct clk **clk;
};

enum {
	MT8167_AFE_IO_INT_ADDA,
};

int mt8167_dai_adda_register(struct mtk_base_afe *afe);

#endif
