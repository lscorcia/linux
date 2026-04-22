/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt8167-afe-clk.h  --  Mediatek 8167 afe clock ctrl definition
 *
 * Author: Luca Leonardo Scorcia <l.scorcia@gmail.com>
 */

#ifndef _MT8167_AFE_CLK_H_
#define _MT8167_AFE_CLK_H_

struct mtk_base_afe;

int mt8167_init_clock(struct mtk_base_afe *afe);
int mt8167_afe_enable_clock(struct mtk_base_afe *afe);
int mt8167_afe_disable_clock(struct mtk_base_afe *afe);
#endif
