// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2023 Spacemit
 */

#include <common.h>
#include <cpu_func.h>
#include <dm.h>
#include <errno.h>
#include <div64.h>
#include <fdtdec.h>
#include <init.h>
#include <log.h>
#include <ram.h>
#include <cpu.h>
#include <asm/cache.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <dm/device_compat.h>
#include <linux/sizes.h>
#include <dt-bindings/soc/spacemit-k1x.h>
#ifdef CONFIG_K1_X_BOARD_FPGA
#include "ddr_init_fpga.h"
#endif

#define DDR_CHECK_SIZE			(0x2000)
#define DDR_CHECK_STEP			(0x2000)
#define DDR_CHECK_CNT			(0x1000)
#define TOP_DDR_NUM				1

static int test_pattern(fdt_addr_t base, fdt_size_t size)
{
	fdt_addr_t addr;
	fdt_size_t check_size;
	uint32_t offset;
	uint32_t *ddr_data = NULL;
	uint32_t *save_data;
	int err = 0;

	check_size = (DDR_CHECK_SIZE / DDR_CHECK_STEP) * DDR_CHECK_CNT;
	ddr_data = malloc(check_size);
	if (!ddr_data) {
		pr_err("test zone malloc fail size 0x%llx\n", check_size);
		return -1;
	}

	save_data = ddr_data;
	/* to avoid overlap important data as image or ramdump  */
	for (addr = base; addr < base + size; addr += DDR_CHECK_STEP) {
		for (offset = 0; offset < DDR_CHECK_CNT; offset += 4) {
			*save_data = readl((void*)addr + offset);
			save_data++;
		}
	}

	for (addr = base; addr < base + size; addr += DDR_CHECK_STEP) {
		for (offset = 0; offset < DDR_CHECK_CNT; offset += 4) {
			writel((uint32_t)(addr + offset), (void*)addr + offset);
		}
	}

	/* writeback and invalid cache */
	flush_dcache_range(base,base+size);
	invalidate_dcache_range(base,base+size);

	for (addr = base; addr < base + size; addr += DDR_CHECK_STEP) {
		for (offset = 0; offset < DDR_CHECK_CNT; offset += 4) {
			if (readl((void*)addr + offset) != (uint32_t)(addr + offset)) {
				pr_debug("ddr check error %x vs %x\n", (uint32_t)(addr + offset), readl((void*)addr + offset));
				err++;
				if (err > 10)
					goto ERR_HANDLE;
			}
		}
	}

	for (addr = base; addr < base + size; addr += DDR_CHECK_STEP) {
		for (offset = 0; offset < DDR_CHECK_CNT; offset += 4) {
			writel((~(uint32_t)(addr + offset)), (void*)addr + offset);
		}
	}

	/* writeback and invalid cache */
	flush_dcache_range(base,base+size);
	invalidate_dcache_range(base,base+size);

	for (addr = base; addr < base + size; addr += DDR_CHECK_STEP) {
		for (offset = 0; offset < DDR_CHECK_CNT; offset += 4) {
			if (readl((void*)addr + offset) != (~(uint32_t)(addr + offset))) {
				pr_debug("ddr check error %x vs %x\n", (uint32_t)(~(addr + offset)), readl((void*)addr + offset));
				err++;
				if (err > 10)
					goto ERR_HANDLE;

			}
		}
	}

ERR_HANDLE:
	save_data = ddr_data;
	for (addr = base; addr < base + size; addr += DDR_CHECK_STEP) {
		for (offset = 0; offset < DDR_CHECK_CNT; offset += 4) {
			writel(*save_data, (void*)addr + offset);
			save_data++;
		}
	}
	if (err != 0) {
		log_err("dram pattern test failed!\n");
	}

	free(ddr_data);

	return err;
}

#ifdef CONFIG_K1_X_BOARD_ASIC
extern void lpddr4_silicon_init(uint32_t base, uint32_t data_rate);
#endif

static uint32_t adjust_cpu_freq(uint64_t cluster, uint32_t freq)
{
	uint32_t freq_act=freq, val;

	/* switch cpu clock source */
	val = readl((void __iomem *)(K1X_APMU_BASE + 0x38c + cluster*4));
	val &= ~(0x07 | BIT(13));
	switch(freq) {
	case 1600000:
		val |= 0x07;
		break;

	case 1228000:
		val |= 0x04;
		break;

	case 819000:
		val |= 0x01;
		break;

	case 614000:
	default:
		freq_act = 614000;
		val |= 0x00;
		break;
	}
	writel(val, (void __iomem *)(K1X_APMU_BASE + 0x38c + cluster*4));

	/* set cluster frequency change request, and wait done */
	val = readl((void __iomem *)(K1X_APMU_BASE + 0x38c + cluster*4));
	val |= BIT(12);
	writel(val, (void __iomem *)(K1X_APMU_BASE + 0x38c + cluster*4));
	while(readl((void __iomem *)(K1X_APMU_BASE + 0x38c + cluster*4)) & BIT(12));

	return freq_act;
}

static int spacemit_ddr_probe(struct udevice *dev)
{
	int ret;

#ifdef CONFIG_K1_X_BOARD_FPGA
	void (*ddr_init)(void);
#else
	uint32_t val, cpu_freq, ddr_datarate;
	fdt_addr_t ddrc_base;
	struct udevice *cpu;

	ddrc_base = dev_read_addr(dev);
#endif

#ifdef CONFIG_K1_X_BOARD_FPGA
	ddr_init = (void(*)(void))(lpddr4_init_fpga_data + 0x144);
	ddr_init();
#else
	writel(0x2dffff, (void __iomem *)0xd4051024);

	/* enable CLK_1228M */
	val = readl((void __iomem *)(K1X_MPMU_BASE + 0x1024));
	val |= BIT(16) | BIT(15) | BIT(14) | BIT(13);
	writel(val, (void __iomem *)(K1X_MPMU_BASE + 0x1024));

	/* enable PLL3(3200Mhz) */
	val = readl((void __iomem *)(K1X_APB_SPARE_BASE + 0x12C));
	val |= BIT(31);
	writel(val, (void __iomem *)(K1X_APB_SPARE_BASE + 0x12C));
	/* enable PLL3_DIV2 */
	val = readl((void __iomem *)(K1X_APB_SPARE_BASE + 0x128));
	val |= BIT(1);
	writel(val, (void __iomem *)(K1X_APB_SPARE_BASE + 0x128));

	cpu = cpu_get_current_dev();
	if(dev_read_u32u(cpu, "boot_freq_cluster0", &cpu_freq)) {
		pr_debug("boot_freq_cluster0 not configured, use 1228000 as default!\n");
		cpu_freq = 1228000;
	}
	cpu_freq = adjust_cpu_freq(0, cpu_freq);
	pr_info("adjust cluster-0 frequency to %u ...	[done]\n", cpu_freq);

	if(dev_read_u32u(cpu, "boot_freq_cluster1", &cpu_freq)) {
		pr_debug("boot_freq_cluster1 not configured, use 1228000 as default!\n");
		cpu_freq = 614000;
	}
	cpu_freq = adjust_cpu_freq(1, cpu_freq);
	pr_info("adjust cluster-1 frequency to %u ...	[done]\n", cpu_freq);

	/* check if dram data-rate is configued in dts */
	if(dev_read_u32u(dev, "datarate", &ddr_datarate)) {
		pr_info("ddr data rate not configed in dts, use 1200 as default!\n");
		ddr_datarate = 1200;
	} else {
		pr_info("ddr data rate is %u configured in dts\n", ddr_datarate);
	}

	/* init dram */
	lpddr4_silicon_init(ddrc_base, ddr_datarate);
#endif

	ret = test_pattern(CONFIG_SYS_SDRAM_BASE, DDR_CHECK_SIZE);
	if (ret < 0) {
		log_err("dram init failed!\n");
		return -EIO;
	}
	pr_debug("dram init done\n");

	return 0;
}

static const struct udevice_id spacemit_ddr_ids[] = {
	{ .compatible = "spacemit,ddr-ctl" },
	{}
};

U_BOOT_DRIVER(spacemit_ddr) = {
	.name = "spacemit_ddr_ctrl",
	.id = UCLASS_RAM,
	.of_match = spacemit_ddr_ids,
	.probe = spacemit_ddr_probe,
};
