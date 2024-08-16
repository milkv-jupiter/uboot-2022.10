// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022-2024 Spacemit
 */

#include <stdio.h>
#include <common.h>
#include <dt-bindings/soc/spacemit-k1x.h>

#define DEBUG_QOS_DUMP

#define K1_DDRC_ADDR(base, offset)	((uintptr_t)(base) + (offset))
#define MC_QOS_CTRL1			K1_DDRC_ADDR(K1X_CIU_BASE, 0x118)
#define MC_QOS_CTRL2			K1_DDRC_ADDR(K1X_CIU_BASE, 0x11c)
#define MC_QOS_CTRL3			K1_DDRC_ADDR(K1X_CIU_BASE, 0x120)
#define MC_QOS_CTRL4			K1_DDRC_ADDR(K1X_CIU_BASE, 0x124)
#define CCI_QOS_CTRL			K1_DDRC_ADDR(K1X_PLAT_CIU_BASE, 0x98)
#define ISP_QOS_CTRL			K1_DDRC_ADDR(K1X_CIU_BASE, 0x1e0)
#define GPU_QOS_CTRL			K1_DDRC_ADDR(K1X_CIU_BASE, 0x100)
#define GNSS_QOS_CTRL			K1_DDRC_ADDR(K1X_CIU_BASE, 0x1c)
#define REG32(x)			(*((volatile uint32_t *)((uintptr_t)(x))))

/* DDR QoS master id */
enum {
	/* port0 */
	DDR_MASTER_CORE = 0,
	DDR_MASTER_GPU,
	DDR_MASTER_GPU_H,
	/* port1 */
	DDR_MASTER_PDMA,
	DDR_MASTER_SDH,
	DDR_MASTER_USB_OTG,
	DDR_MASTER_USB2,
	DDR_MASTER_USB3,
	DDR_MASTER_EMAC0,
	DDR_MASTER_EMAC1,
	/* port2 */
	DDR_MASTER_VPU,
	DDR_MASTER_PCIE_A,
	DDR_MASTER_PCIE_B,
	DDR_MASTER_PCIE_C,
	/* port3 */
	DDR_MASTER_V2D,
	DDR_MASTER_CCIC,
	DDR_MASTER_JPEG,
	DDR_MASTER_CPP,
	DDR_MASTER_ISP,
};

/* defined in ciu reg, bit width = 4 by default */
struct ciu_qos_conf {
	const char *name;
	uint32_t qos_id;
	uint32_t reg;
	uint32_t aw_qos;
	uint32_t aw_offset;
	uint32_t ar_qos;
	uint32_t ar_offset;
	uint32_t mask;
};

/* QoS on DDR port
* lcd hdmi qos and v2d qos are in their drvier
*/
static struct ciu_qos_conf qos_conf[] = {
	/*name 		id			reg	      wqos	woffset	rqos	roffset mask */
	{"CORE",	DDR_MASTER_CORE,	CCI_QOS_CTRL,	0,	0,	0,	4,	0xff},
	{"GPU",		DDR_MASTER_GPU,		GPU_QOS_CTRL,	1,	8,	1,	12,	0xff},
	{"GPU_H",	DDR_MASTER_GPU_H,	GPU_QOS_CTRL,	1,	16,	1,	20,	0xff},

	{"PDMA",	DDR_MASTER_PDMA,	MC_QOS_CTRL1,	0,	16,	0,	20,	0xff},
	{"USB3",	DDR_MASTER_USB3,	MC_QOS_CTRL1,	1,	8,	1,	12,	0xff},
	{"SDH",		DDR_MASTER_SDH,		MC_QOS_CTRL2,	1,	0,	1,	4,	0xff},
	{"USB_OTG",	DDR_MASTER_USB_OTG,	MC_QOS_CTRL2,	1,	8,	1,	12,	0xff},
	{"USB2",	DDR_MASTER_USB2,	MC_QOS_CTRL2,	1,	16,	1,	20,	0xff},
	{"EMAC0",	DDR_MASTER_EMAC0,	MC_QOS_CTRL3,	1,	0,	1,	0,	0xf},
	{"EMAC1",	DDR_MASTER_EMAC1,	MC_QOS_CTRL3,	1,	0,	1,	0,	0xf},

	{"PCIE_A",	DDR_MASTER_PCIE_A,	MC_QOS_CTRL3,	2,	8,	2,	12,	0xff},
	{"PCIE_B",	DDR_MASTER_PCIE_B,	MC_QOS_CTRL3,	2,	16,	2,	20,	0xff},
	{"PCIE_C",	DDR_MASTER_PCIE_C,	MC_QOS_CTRL3,	2,	24,	2,	28,	0xff},
	{"VPU",		DDR_MASTER_VPU,		MC_QOS_CTRL4,	1,	24,	1,	28,	0xff},

	{"CCIC",	DDR_MASTER_CCIC,	ISP_QOS_CTRL,	3,	24,	3,	28,	0xff},
	{"JPEG",	DDR_MASTER_JPEG,	ISP_QOS_CTRL,	2,	16,	2,	20,	0xff},
	{"CPP",		DDR_MASTER_CPP,		ISP_QOS_CTRL,	2,	8,	2,	12,	0xff},
	{"ISP",		DDR_MASTER_ISP,		ISP_QOS_CTRL,	3,	0,	2,	4,	0xff},
};

static void qos_setup_one(struct ciu_qos_conf *qos_conf)
{
	uint32_t reg, val, mask, shift, qos_val;

	reg = qos_conf->reg;
	mask = qos_conf->mask;
	/* qos val = (aw_val<<aw_offset)|(ar_val<<(aw_offset+ar_offset)) */
	shift = qos_conf->ar_offset;
	qos_val = qos_conf->ar_qos << shift;
	shift = qos_conf->aw_offset;
	qos_val |= (qos_conf->aw_qos << shift);

	val = REG32(reg);
	shift = min(qos_conf->ar_offset, qos_conf->aw_offset);
	val &= ~(mask << shift);
	val |= qos_val;
	REG32(reg) = val;
}

static void qos_setup(void)
{
	int i;

	REG32(MC_QOS_CTRL2) &= ~(1 << 26);
	REG32(MC_QOS_CTRL2) |= (1 << 25);
	REG32(MC_QOS_CTRL2) |= (1 << 24);
	for (i = 0; i < ARRAY_SIZE(qos_conf); i++)
		qos_setup_one(&qos_conf[i]);
}

#ifdef DEBUG_QOS_DUMP
static void qos_dump_one(struct ciu_qos_conf *qos_conf)
{
	uint32_t reg, val, mask, shift;
	uint32_t rqos;
	uint32_t wqos;

	reg = qos_conf->reg;
	mask = qos_conf->mask;
	shift = min(qos_conf->ar_offset, qos_conf->aw_offset);

	val = REG32(reg);
	val &= (mask << shift);

	shift = qos_conf->aw_offset;
	wqos = (val >> shift) & 0xf;
	shift = qos_conf->ar_offset;
	rqos = (val >> shift) & 0xf;

	printf("%s: rd = %d wr = %d\n", qos_conf->name, rqos, wqos);
}
#endif

static void qos_dump(void)
{
#ifdef DEBUG_QOS_DUMP
	int i;

	for (i = 0; i < ARRAY_SIZE(qos_conf); i++)
		qos_dump_one(&qos_conf[i]);
#endif
}

void qos_set_default(void)
{
	qos_setup();
	qos_dump();
}
