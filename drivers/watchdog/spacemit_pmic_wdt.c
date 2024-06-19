// SPDX-License-Identifier: GPL-2.0+

#include <common.h>
#include <dm.h>
#include <errno.h>
#include <wdt.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <dm/device_compat.h>
#include <power/spacemit/spacemit_pmic.h>
#include <power/pmic.h>

enum pmic_model {
	PMIC_MODEL_UNKNOWN,
	PMIC_MODEL_SPM8821,
};

struct pmic_wdt_priv {
	struct udevice *pmic_dev;
	fdt_addr_t base;
	fdt_addr_t pwr_ctrl0;
	fdt_addr_t pwr_ctrl2;
	enum pmic_model model;
};

static int pmic_wdt_initialize(struct udevice *dev, u64 timeout_ms)
{
	struct pmic_wdt_priv *priv = dev_get_priv(dev);
	if (!priv) {
		pr_err("Failed to get private data\n");
		return -ENODEV;
	}

	int ret;
	uint timeout_val;
	uint reg_val;

	switch (priv->model) {
	case PMIC_MODEL_SPM8821:
		if (timeout_ms <= 1000) {
			timeout_val = SPM8821_WDT_TIMEOUT_1S;
		} else if (timeout_ms <= 4000) {
			timeout_val = SPM8821_WDT_TIMEOUT_4S;
		} else if (timeout_ms <= 8000) {
			timeout_val = SPM8821_WDT_TIMEOUT_8S;
		} else {
			timeout_val = SPM8821_WDT_TIMEOUT_16S;
		}

		// Clear the status
		ret = pmic_reg_read(priv->pmic_dev, priv->base);
		if (ret < 0) return ret;
		reg_val = ret | SPM8821_WDT_CLEAR_STATUS;
		ret = pmic_reg_write(priv->pmic_dev, priv->base, reg_val);
		if (ret) return ret;

		// Set the timeout value
		ret = pmic_reg_read(priv->pmic_dev, priv->base);
		if (ret < 0) return ret;
		reg_val = (reg_val & ~(0x3 << 1)) | (timeout_val << 1);
		ret = pmic_reg_write(priv->pmic_dev, priv->base, reg_val);
		if (ret) return ret;

		// Enable the watchdog
		ret = pmic_reg_read(priv->pmic_dev, priv->base);
		if (ret < 0) return ret;
		reg_val |= SPM8821_WDT_ENABLE;
		ret = pmic_reg_write(priv->pmic_dev, priv->base, reg_val);
		if (ret) return ret;

		// Enable watchdog reset
		ret = pmic_reg_read(priv->pmic_dev, priv->pwr_ctrl0);
		if (ret < 0) return ret;
		reg_val = ret | SPM8821_WDT_RESET_ENABLE;
		ret = pmic_reg_write(priv->pmic_dev, priv->pwr_ctrl0, reg_val);
		if (ret) return ret;

		break;
	default:
		pr_err("Unsupported PMIC model: %d\n", priv->model);
		return -EINVAL;
	}

	return 0;
}

static int pmic_wdt_reset(struct udevice *dev)
{
	struct pmic_wdt_priv *priv = dev_get_priv(dev);
	if (!priv) {
		pr_err("Failed to get private data\n");
		return -ENODEV;
	}

	int ret;
	uint reg_val;

	switch (priv->model) {
	case PMIC_MODEL_SPM8821:
		/* Clear watchdog timer status */
		ret = pmic_reg_read(priv->pmic_dev, priv->base);
		if (ret < 0) return ret;
		reg_val = ret | SPM8821_WDT_CLEAR_STATUS;
		ret = pmic_reg_write(priv->pmic_dev, priv->base, reg_val);
		if (ret) return ret;

		ret = pmic_reg_read(priv->pmic_dev, priv->pwr_ctrl0);
		if (ret < 0) return ret;
		reg_val = ret | 0x1;
		ret = pmic_reg_write(priv->pmic_dev, priv->pwr_ctrl0, reg_val);
		if (ret) return ret;

		break;
	default:
		pr_err("Unsupported PMIC model: %d\n", priv->model);
		return -EINVAL;
	}

	return 0;
}

static int pmic_wdt_start(struct udevice *dev, u64 timeout_ms, ulong flags)
{
	struct pmic_wdt_priv *priv = dev_get_priv(dev);
	if (!priv) {
		pr_err("Failed to get private data\n");
		return -ENODEV;
	}

	int ret = pmic_wdt_initialize(dev, timeout_ms);
	if (ret) {
		return ret;
	}

	uint value;

	switch (priv->model) {
	case PMIC_MODEL_SPM8821:
		/* Start watchdog timer */
		ret = pmic_reg_read(priv->pmic_dev, priv->base);
		if (ret < 0) return ret;

		value = ret | SPM8821_WDT_ENABLE;
		ret = pmic_reg_write(priv->pmic_dev, priv->base, value);
		if (ret) return ret;

		break;
	default:
		pr_err("Unsupported PMIC model: %d\n", priv->model);
		return -EINVAL;
	}

	return 0;
}

static int pmic_wdt_stop(struct udevice *dev)
{
	struct pmic_wdt_priv *priv = dev_get_priv(dev);
	if (!priv) {
		pr_err("Failed to get private data\n");
		return -ENODEV;
	}

	int ret;
	uint reg_val;

	switch (priv->model) {
	case PMIC_MODEL_SPM8821:
		ret = pmic_reg_read(priv->pmic_dev, priv->base);
		if (ret < 0) return ret;
		reg_val = ret & ~SPM8821_WDT_ENABLE;
		ret = pmic_reg_write(priv->pmic_dev, priv->base, reg_val);
		if (ret) return ret;

		break;
	default:
		pr_err("Unsupported PMIC model: %d\n", priv->model);
		return -EINVAL;
	}

	return 0;
}

static int pmic_wdt_remove(struct udevice *dev)
{
	return pmic_wdt_stop(dev);
}

static int pmic_wdt_expire_now(struct udevice *dev, ulong flags)
{
	struct pmic_wdt_priv *priv = dev_get_priv(dev);
	if (!priv) {
		pr_err("Failed to get private data\n");
		return -ENODEV;
	}

	int ret;
	uint reg_val;

	switch (priv->model) {
	case PMIC_MODEL_SPM8821:
		/* Set SW_RST bit of PWR_CTRL2 */
		ret = pmic_reg_read(priv->pmic_dev, priv->pwr_ctrl2);
		if (ret < 0) return ret;
		reg_val = ret | SPM8821_SW_RST;
		ret = pmic_reg_write(priv->pmic_dev, priv->pwr_ctrl2, reg_val);
		if (ret) return ret;

		mdelay(100);

		break;
	default:
		pr_err("Unsupported PMIC model: %d\n", priv->model);
		return -EINVAL;
	}

	return -ENODEV;
}

static int pmic_wdt_probe(struct udevice *dev)
{
	struct pmic_wdt_priv *priv = dev_get_priv(dev);
	if (!priv)
		return -ENOMEM;

	const char *wdt_name = dev_read_string(dev, "wdt-name");
	if (!wdt_name) {
		pr_err("Failed to read wdt-name string\n");
		return -EINVAL;
	}

	if (strcmp(wdt_name, "wdt_pm8821") == 0) {
		priv->model = PMIC_MODEL_SPM8821;
		priv->base = SPM8821_WDT_CTRL;
		priv->pwr_ctrl0 = SPM8821_PWR_CTRL0;
		priv->pwr_ctrl2 = SPM8821_PWR_CTRL2;
	} else {
		priv->model = PMIC_MODEL_UNKNOWN;
		pr_err("Device is not compatible: %s\n", wdt_name);
		return -EINVAL;
	}

	/* Get parent PMIC device */
	priv->pmic_dev = dev_get_parent(dev);
	if (!priv->pmic_dev) {
		pr_err("Failed to get parent PMIC device\n");
		return -ENODEV;
	}

	return 0;
}

static struct wdt_ops pmic_wdt_ops = {
	.start = pmic_wdt_start,
	.stop = pmic_wdt_stop,
	.reset = pmic_wdt_reset,
	.expire_now = pmic_wdt_expire_now,
};

static const struct udevice_id pmic_wdt_ids[] = {
	{ .compatible = "spacemit,k1x-pm8821-wdt" },
	{ /* sentinel */ }
};

U_BOOT_DRIVER(pm8xx_wdt) = {
	.name = "pm8xx_wdt",
	.id = UCLASS_WDT,
	.of_match = pmic_wdt_ids,
	.probe = pmic_wdt_probe,
	.priv_auto = sizeof(struct pmic_wdt_priv),
	.ops = &pmic_wdt_ops,
	.remove = pmic_wdt_remove,
	.flags  = DM_FLAG_OS_PREPARE,
};
