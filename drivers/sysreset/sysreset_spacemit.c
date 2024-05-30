/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2023, Spacemit
 */

#include <common.h>
#include <dm.h>
#include <errno.h>
#include <sysreset.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <power/pmic.h>

struct pmic_sysreset_data {
	u32 reboot_reg;
	u32 reboot_mask;
	int type;
};

static int pmic_sysreboot_types(const char *compat) {
	if (!strcmp(compat, "spacemit,spm8821-reset"))
		return 1;
	return 0;
}

static int pmic_sysreset_request(struct udevice *dev, enum sysreset_t type)
{
	struct pmic_sysreset_data *data = dev_get_priv(dev);
	struct udevice *pmic_dev;
	int ret, value;

	ret = uclass_get_device(UCLASS_PMIC, 0, &pmic_dev);
	if (ret) {
		pr_err("Failed to find PMIC device\n");
		return ret;
	}

	switch (data->type) {
		case 1:
			value = pmic_reg_read(pmic_dev, data->reboot_reg);
			if (ret) {
				pr_err("Failed to read reboot register for spm8821: %d\n", ret);
				return ret;
			}
			value |= data->reboot_mask;
			ret = pmic_reg_write(pmic_dev, data->reboot_reg, value);
			if (ret) {
				pr_err("Failed to write reboot register for spm8821: %d\n", ret);
				return ret;
			}
			break;
		default:
			pr_err("Unsupported PMIC type for sysreset\n");
			return -ENOSYS;
	}

	mdelay(100);
	return -EINPROGRESS;
}

static int pmic_sysreset_probe(struct udevice *dev)
{
	struct pmic_sysreset_data *data = dev_get_priv(dev);
	const char *compat = dev_read_string(dev, "compatible");

	data->reboot_reg = dev_read_u32_default(dev, "reboot-reg", 0);
	data->reboot_mask = dev_read_u32_default(dev, "reboot-mask", 0);
	data->type = pmic_sysreboot_types(compat);

	return 0;
}

static struct sysreset_ops pmic_sysreset_ops = {
	.request = pmic_sysreset_request,
};

U_BOOT_DRIVER(pmic_sysreset) = {
	.name   = "pmic_sysreset",
	.id     = UCLASS_SYSRESET,
	.priv_auto = sizeof(struct pmic_sysreset_data),
	.ops    = &pmic_sysreset_ops,
	.probe  = pmic_sysreset_probe,
};
