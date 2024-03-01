// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, spacemit Corporation.
 *
 */

#ifndef _CCU_SPACEMIT_K1X_H_
#define _CCU_SPACEMIT_K1X_H_

#include <clk.h>
#include <clk-uclass.h>
#include <dt-bindings/clock/spacemit-k1x-clock.h>

#define SPACEMIT_CLK_NO_PARENT "clk_dummy"

enum ccu_base_type{
	BASE_TYPE_MPMU       = 0,
	BASE_TYPE_APMU       = 1,
	BASE_TYPE_APBC       = 2,
	BASE_TYPE_APBS       = 3,
	BASE_TYPE_CIU        = 4,
	BASE_TYPE_DCIU       = 5,
	BASE_TYPE_DDRC       = 6,
	BASE_TYPE_AUDC       = 7,
	BASE_TYPE_APBC2      = 8,
};

enum {
	CLK_DIV_TYPE_1REG_NOFC_V1 = 0,
	CLK_DIV_TYPE_1REG_FC_V2,
	CLK_DIV_TYPE_2REG_NOFC_V3,
	CLK_DIV_TYPE_2REG_FC_V4,
	CLK_DIV_TYPE_1REG_FC_DIV_V5,
	CLK_DIV_TYPE_1REG_FC_MUX_V6,
};

struct ccu_common {
	void __iomem	*base;
	enum ccu_base_type base_type;
	u32 	reg_type;
	u32 	reg_ctrl;
	u32 	reg_sel;
	u32 	reg_xtc;
	u32 	fc;
	bool	is_pll;
	const char		*name;
	const char		*driver_name;
	const char		*parent_name;
	const char		* const *parent_names;
	u8	num_parents;
	unsigned long	flags;
	struct clk	clk;
	struct spacemit_clk_table * clk_tbl;
};

struct spacemit_k1x_clk {
	void __iomem *mpmu_base;
	void __iomem *apmu_base;
	void __iomem *apbc_base;
	void __iomem *apbs_base;
	void __iomem *ciu_base;
	void __iomem *dciu_base;
	void __iomem *ddrc_base;
	void __iomem *audio_ctrl_base;
	void __iomem *apbc2_base;
};

struct spacemit_clk_table{
	struct clk* clks[CLK_MAX_NO];
	unsigned int num;
};

struct spacemit_clk_init_rate{
	u32 clk_id;
	unsigned int dft_rate;
};

static inline struct ccu_common *clk_to_ccu_common(struct clk *clk)
{
	return container_of(clk, struct ccu_common, clk);
}

int spacemit_ccu_probe(struct spacemit_k1x_clk *clk_info,
		    struct spacemit_clk_table *clks);

#endif /* _CCU_SPACEMIT_K1X_H_ */
