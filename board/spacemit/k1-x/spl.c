// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2023 Spacemit, Inc
 */

#include <common.h>
#include <dm.h>
#include <init.h>
#include <spl.h>
#include <misc.h>
#include <log.h>
#include <i2c.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <env.h>
#include <env_internal.h>
#include <mapmem.h>
#include <asm/global_data.h>
#include <fb_spacemit.h>
#include <tlv_eeprom.h>
#include <stdlib.h>

#define GEN_CNT			(0xD5001000)
#define STORAGE_API_P_ADDR	(0xC0838498)
#define SDCARD_API_ENTRY	(0xFFE0A548)

/* pin mux */
#define MUX_MODE0       0
#define MUX_MODE1       1
#define MUX_MODE2       2
#define MUX_MODE3       3
#define MUX_MODE4       4
#define MUX_MODE5       5
#define MUX_MODE6       6
#define MUX_MODE7       7

/* edge detect */
#define EDGE_NONE       (1 << 6)
#define EDGE_RISE       (1 << 4)
#define EDGE_FALL       (1 << 5)
#define EDGE_BOTH       (3 << 4)

/* driver strength*/
#define PAD_1V8_DS0     (0 << 11)
#define PAD_1V8_DS1     (1 << 11)
#define PAD_1V8_DS2     (2 << 11)
#define PAD_1V8_DS3     (3 << 11)

/*
 * notice: !!!
 * ds2 ---> bit10, ds1 ----> bit12, ds0 ----> bit11
*/
#define PAD_3V_DS0      (0 << 10)     /* bit[12:10] 000 */
#define PAD_3V_DS1      (2 << 10)     /* bit[12:10] 010 */
#define PAD_3V_DS2      (4 << 10)     /* bit[12:10] 100 */
#define PAD_3V_DS3      (6 << 10)     /* bit[12:10] 110 */
#define PAD_3V_DS4      (1 << 10)     /* bit[12:10] 001 */
#define PAD_3V_DS5      (3 << 10)     /* bit[12:10] 011 */
#define PAD_3V_DS6      (5 << 10)     /* bit[12:10] 101 */
#define PAD_3V_DS7      (7 << 10)     /* bit[12:10] 111 */

/* pull up/down */
#define PULL_DIS        (0 << 13)     /* bit[15:13] 000 */
#define PULL_UP         (6 << 13)     /* bit[15:13] 110 */
#define PULL_DOWN       (5 << 13)     /* bit[15:13] 101 */

#define MFPR_MMC1_BASE     0xD401E1B8
#define MMC1_DATA3_OFFSET  0x00
#define MMC1_DATA2_OFFSET  0x04
#define MMC1_DATA1_OFFSET  0x08
#define MMC1_DATA0_OFFSET  0x0C
#define MMC1_CMD_OFFSET    0x10
#define MMC1_CLK_OFFSET    0x14

extern int k1x_eeprom_init(void);
extern int spacemit_eeprom_read(uint8_t chip, uint8_t *buffer, uint8_t id);
char *product_name;

int timer_init(void)
{
	/* enable generic cnt */
	u32 read_data;
	void __iomem *reg;

	reg = ioremap(GEN_CNT, 0x20);
	read_data = readl(reg);
	read_data |= BIT(0);
	writel(read_data, reg);

	return 0;
}

enum board_boot_mode get_boot_storage(void)
{
	size_t *api = (size_t*)STORAGE_API_P_ADDR;
	size_t address = *api;
	// Did NOT select sdcard boot, but sdcard always has first boot priority
	if (SDCARD_API_ENTRY == address)
		return BOOT_MODE_SD;
	else
		return get_boot_pin_select();
}

void fix_boot_mode(void)
{
	if (0 == readl((void *)BOOT_DEV_FLAG_REG))
		set_boot_mode(get_boot_storage());
}

void board_pinctrl_setup(void)
{
	//sdcard pinctrl setup
	writel(MUX_MODE0 | EDGE_NONE | PULL_UP | PAD_3V_DS4, (void __iomem *)MFPR_MMC1_BASE + MMC1_DATA3_OFFSET);
	writel(MUX_MODE0 | EDGE_NONE | PULL_UP | PAD_3V_DS4, (void __iomem *)MFPR_MMC1_BASE + MMC1_DATA2_OFFSET);
	writel(MUX_MODE0 | EDGE_NONE | PULL_UP | PAD_3V_DS4, (void __iomem *)MFPR_MMC1_BASE + MMC1_DATA1_OFFSET);
	writel(MUX_MODE0 | EDGE_NONE | PULL_UP | PAD_3V_DS4, (void __iomem *)MFPR_MMC1_BASE + MMC1_DATA0_OFFSET);
	writel(MUX_MODE0 | EDGE_NONE | PULL_UP | PAD_3V_DS4, (void __iomem *)MFPR_MMC1_BASE + MMC1_CMD_OFFSET);
	writel(MUX_MODE0 | EDGE_NONE | PULL_DOWN | PAD_3V_DS4, (void __iomem *)MFPR_MMC1_BASE + MMC1_CLK_OFFSET);
}

#if CONFIG_IS_ENABLED(SPACEMIT_K1X_EFUSE)
int load_board_config_from_efuse(int *eeprom_i2c_index,
				 int *eeprom_pin_group, int *pmic_type)
{
	struct udevice *dev;
	uint8_t fuses[2];
	int ret;

	/* retrieve the device */
	ret = uclass_get_device_by_driver(UCLASS_MISC,
			DM_DRIVER_GET(spacemit_k1x_efuse), &dev);
	if (ret) {
		return ret;
	}

	// read from efuse, each bank has 32byte efuse data
	ret = misc_read(dev, 9 * 32 + 0, fuses, sizeof(fuses));
	if ((0 == ret) && (0 != fuses[0])) {
		// byte0 bit0~3 is eeprom i2c controller index
		*eeprom_i2c_index = fuses[0] & 0x0F;
		// byte0 bit4~5 is eeprom pin group index
		*eeprom_pin_group = (fuses[0] >> 4) & 0x03;
		// byte1 bit0~3 is pmic type
		*pmic_type = fuses[1] & 0x0F;
	}

	return ret;
}
#endif

static void load_default_board_config(int *eeprom_i2c_index,
		int *eeprom_pin_group, int *pmic_type)
{
	char *temp;

	temp = env_get("eeprom_i2c_index");
	if (NULL != temp)
		*eeprom_i2c_index = dectoul(temp, NULL);
	else
		*eeprom_i2c_index = K1_DEFALT_EEPROM_I2C_INDEX;

	temp = env_get("eeprom_pin_group");
	if (NULL != temp)
		*eeprom_pin_group = dectoul(temp, NULL);
	else
		*eeprom_pin_group = K1_DEFALT_EEPROM_PIN_GROUP;

	temp = env_get("pmic_type");
	if (NULL != temp)
		*pmic_type = dectoul(temp, NULL);
	else
		*pmic_type = K1_DEFALT_PMIC_TYPE;
}

#if CONFIG_IS_ENABLED(SPACEMIT_POWER)
extern int board_pmic_init(void);
#endif

void load_board_config(int *eeprom_i2c_index, int *eeprom_pin_group, int *pmic_type)
{
	load_default_board_config(eeprom_i2c_index, eeprom_pin_group, pmic_type);

#if CONFIG_IS_ENABLED(SPACEMIT_K1X_EFUSE)
	/* update env from efuse data */
	load_board_config_from_efuse(eeprom_i2c_index, eeprom_pin_group, pmic_type);
#endif

	pr_debug("eeprom_i2c_index :%d\n", *eeprom_i2c_index);
	pr_debug("eeprom_pin_group :%d\n", *eeprom_pin_group);
	pr_debug("pmic_type :%d\n", *pmic_type);
}

int spl_board_init_f(void)
{
	int ret;
	struct udevice *dev;

#if CONFIG_IS_ENABLED(SYS_I2C_LEGACY)
	/* init i2c */
	i2c_init_board();
#endif

#if CONFIG_IS_ENABLED(SPACEMIT_POWER)
	board_pmic_init();
#endif

	/* DDR init */
	ret = uclass_get_device(UCLASS_RAM, 0, &dev);
	if (ret) {
		pr_err("DRAM init failed: %d\n", ret);
		return ret;
	}

	timer_init();

	return 0;
}

void board_init_f(ulong dummy)
{
	int ret;

	// fix boot mode after boot rom
	fix_boot_mode();

	// setup pinctrl
	board_pinctrl_setup();

	ret = spl_early_init();
	if (ret)
		panic("spl_early_init() failed: %d\n", ret);

	riscv_cpu_setup(NULL, NULL);

	preloader_console_init();
	pr_debug("boot_mode: %x\n", get_boot_mode());

	ret = spl_board_init_f();
	if (ret)
		panic("spl_board_init_f() failed: %d\n", ret);
}

#ifdef CONFIG_SPL_LOAD_FIT
int board_fit_config_name_match(const char *name)
{
	char *buildin_name;

	buildin_name = product_name;
	if (NULL == buildin_name)
		buildin_name = env_get("product_name");

	if ((NULL != buildin_name) && (0 == strcmp(buildin_name, name))) {
		pr_debug("Boot from fit configuration %s\n", name);
		return 0;
	}
	else
		return -1;
}
#endif

static struct env_driver *_spl_env_driver_lookup(enum env_location loc)
{
	struct env_driver *drv;
	const int n_ents = ll_entry_count(struct env_driver, env_driver);
	struct env_driver *entry;

	drv = ll_entry_start(struct env_driver, env_driver);
	for (entry = drv; entry != drv + n_ents; entry++) {
		if (loc == entry->location)
			return entry;
	}

	/* Not found */
	return NULL;
}

static struct env_driver *spl_env_driver_lookup(enum env_operation op, enum env_location loc)
{
	struct env_driver *drv;

	if (loc == ENVL_UNKNOWN)
		return NULL;

	drv = _spl_env_driver_lookup(loc);
	if (!drv) {
		pr_debug("%s: No environment driver for location %d\n", __func__, loc);
		return NULL;
	}

	return drv;
}

static void spl_load_env(void)
{
	struct env_driver *drv;
	int ret = -1;
	u32 boot_mode = get_boot_mode();

	/*if boot from usb, spl should not find env*/
	if (boot_mode == BOOT_MODE_USB){
		return;
	}

	/*
	only load env from mtd dev, because only mtd dev need
	env mtdparts info to load image.
	*/
	enum env_location loc = ENVL_UNKNOWN;
	switch (boot_mode) {
#ifdef CONFIG_ENV_IS_IN_NAND
	case BOOT_MODE_NAND:
		loc = ENVL_NAND;
		break;
#endif
#ifdef CONFIG_ENV_IS_IN_SPI_FLASH
	case BOOT_MODE_NOR:
		loc = ENVL_SPI_FLASH;
		break;
#endif
#ifdef CONFIG_ENV_IS_IN_MTD
	case BOOT_MODE_NAND:
	case BOOT_MODE_NOR:
		loc = ENVL_MTD;
		break;
#endif
	default:
		return;
	}

	drv = spl_env_driver_lookup(ENVOP_INIT, loc);
	if (!drv){
		pr_err("%s, can not load env from storage\n", __func__);
		return;
	}

	ret = drv->load();
	if (!ret){
		pr_info("has init env successful\n");
	}else{
		pr_err("load env from storage fail, would use default env\n");
		/*if load env from storage fail, it should not write bootmode to reg*/
		boot_mode = BOOT_MODE_NONE;
	}
}

char *get_product_name(void)
{
	char *name = NULL;
	int eeprom_addr;

	eeprom_addr = k1x_eeprom_init();
	name = calloc(1, 64);
	if ((eeprom_addr >= 0) && (NULL != name) && (0 == spacemit_eeprom_read(
		eeprom_addr, name, TLV_CODE_PRODUCT_NAME))) {
		pr_info("Get product name from eeprom %s\n", name);
		return name;
	}

	if (NULL != name)
		free(name);

	pr_debug("Use default product name %s\n", env_get("product_name"));
	return NULL;
}

void spl_board_init(void)
{
	/*load env*/
	spl_load_env();
	product_name = get_product_name();
}

struct image_header *spl_get_load_buffer(ssize_t offset, size_t size)
{
	return map_sysmem(CONFIG_SPL_LOAD_FIT_ADDRESS, 0);
}

void board_boot_order(u32 *spl_boot_list)
{
	u32 boot_mode = get_boot_mode();
	pr_debug("boot_mode:%x\n", boot_mode);
	if (boot_mode == BOOT_MODE_USB){
		spl_boot_list[0] = BOOT_DEVICE_BOARD;
	}else{
		switch (boot_mode) {
		case BOOT_MODE_EMMC:
			spl_boot_list[0] = BOOT_DEVICE_MMC2;
			break;
		case BOOT_MODE_NAND:
			spl_boot_list[0] = BOOT_DEVICE_NAND;
			break;
		case BOOT_MODE_NOR:
			spl_boot_list[0] = BOOT_DEVICE_NOR;
			break;
		case BOOT_MODE_SD:
			spl_boot_list[0] = BOOT_DEVICE_MMC1;
			break;
		default:
			spl_boot_list[0] = BOOT_DEVICE_RAM;
			break;
		}

		//reserve for debug/test to load/run uboot from ram.
		spl_boot_list[1] = BOOT_DEVICE_RAM;
	}
}
