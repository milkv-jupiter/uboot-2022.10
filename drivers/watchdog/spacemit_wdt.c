#include <common.h>
#include <dm.h>
#include <errno.h>
#include <wdt.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <clk.h>
#include <reset-uclass.h>

/* Register offsets relative to the base address of the WDT */
#define WDT_ENABLE_OFFSET   0xB8
#define WDT_TIMEOUT_OFFSET  0xBC
#define WDT_RESET_OFFSET    0xC8
#define WDT_STATUS_OFFSET   0xC0
#define WDT_WSAR_OFFSET     0xB4
#define WDT_WFAR_OFFSET     0xB0

/* Watchdog Timer Control Bits */
#define WDT_CLEAR_STATUS     0x0
#define WDT_RESET_ENABLE     0x1
#define WDT_ENABLE           0x3
#define WDT_TIMEOUT          0x1

/* WDT Start Register and its enable bit */
#define WDT_START_ENABLE     BIT(4)
#define WDT_CLK_FREQ 256

struct spacemit_wdt_priv {
	struct clk clk;
	struct reset_ctl reset;
	fdt_addr_t base;
	fdt_addr_t start_reg;
};

static void spa_wdt_write_access(fdt_addr_t base)
{
	writel(0xbaba, (void *)(base + WDT_WFAR_OFFSET));
	writel(0xeb10, (void *)(base + WDT_WSAR_OFFSET));
}

static void spa_wdt_write(u32 val, void *reg, fdt_addr_t base)
{
	spa_wdt_write_access(base);
	writel(val, reg);
}

static int initialize_wdt(struct udevice *dev, u64 timeout_ms)
{
	struct spacemit_wdt_priv *priv = dev_get_priv(dev);
	if (!priv) {
		pr_err("Failed to get private data\n");
		return -ENODEV;
	}

	int ret;
	u64 counter = (timeout_ms * WDT_CLK_FREQ) / 1000;

	// Enable the clock
	ret = clk_enable(&priv->clk);
	if (ret) {
		pr_err("Failed to enable clock: %d\n", ret);
		return ret;
	}

	// Deassert the reset
	ret = reset_deassert(&priv->reset);
	if (ret) {
		pr_err("Failed to deassert reset: %d\n", ret);
		return ret;
	}

	u32 timeout_val = readl((void *)(priv->base + WDT_TIMEOUT_OFFSET));

	/* Set watchdog timer parameters */
	spa_wdt_write(WDT_CLEAR_STATUS, (void *)(priv->base + WDT_STATUS_OFFSET), priv->base);
	spa_wdt_write(counter, (void *)(priv->base + WDT_TIMEOUT_OFFSET), priv->base);
	spa_wdt_write(WDT_ENABLE, (void *)(priv->base + WDT_ENABLE_OFFSET), priv->base);
	spa_wdt_write(WDT_RESET_ENABLE, (void *)(priv->base + WDT_RESET_OFFSET), priv->base);

	timeout_val = readl((void *)(priv->base + WDT_TIMEOUT_OFFSET));

	return 0;
}

static int spacemit_wdt_reset(struct udevice *dev)
{
	struct spacemit_wdt_priv *priv = dev_get_priv(dev);
	if (!priv) {
		pr_err("Failed to get private data\n");
		return -ENODEV;
	}

	/* Clear watchdog timer status only, do not set timeout */
	spa_wdt_write(WDT_CLEAR_STATUS, (void *)(priv->base + WDT_STATUS_OFFSET), priv->base);
	spa_wdt_write(WDT_RESET_ENABLE, (void *)(priv->base + WDT_RESET_OFFSET), priv->base);

	return 0;
}


static int spacemit_wdt_start(struct udevice *dev, u64 timeout_ms, ulong flags)
{
	struct spacemit_wdt_priv *priv = dev_get_priv(dev);
	if (!priv) {
		pr_err("Failed to get private data\n");
		return -ENODEV;
	}

	int ret = initialize_wdt(dev, timeout_ms);
	if (ret) {
		return ret;
	}

	/* Start watchdog timer */
	u32 reg = readl((void*)priv->start_reg);
	writel(WDT_START_ENABLE | reg, (void *)priv->start_reg);

	return 0;
}

static int spacemit_wdt_stop(struct udevice *dev)
{
	struct spacemit_wdt_priv *priv = dev_get_priv(dev);
	if (!priv) {
		pr_err("Failed to get private data\n");
		return -ENODEV;
	}

	// Stop the watchdog timer by clearing the enable bit.
	spa_wdt_write(0x0, (void *)(priv->base + WDT_ENABLE_OFFSET), priv->base);
	return 0;
}

static int spacemit_wdt_remove(struct udevice *dev)
{
	return spacemit_wdt_stop(dev);
}

static int spacemit_wdt_expire_now(struct udevice *dev, ulong flags)
{
	initialize_wdt(dev, 0);
	return 0;
}

static int spacemit_wdt_probe(struct udevice *dev)
{
	int ret;
	struct spacemit_wdt_priv *priv = malloc(sizeof(*priv));
	if (!priv)
		return -ENOMEM;

	dev->priv_ = priv;

	/* Retrieve and store the reset reference */
	ret = reset_get_by_index(dev, 0, &priv->reset);
	if (ret) {
		pr_err("Failed to get the first reset control: %d\n", ret);
		return ret;
	}

	/* Retrieve and store the clock reference */
	ret = clk_get_by_index(dev, 0, &priv->clk);
	if (ret) {
		pr_err("Failed to get the first clock source: %d\n", ret);
		return ret;
	}

	priv->base = dev_read_addr(dev);
	if (priv->base == FDT_ADDR_T_NONE) {
		pr_err("Failed to get WDT base address\n");
		return -EINVAL;
	}

	priv->start_reg = dev_read_addr_index(dev, 1);
	if (priv->start_reg == FDT_ADDR_T_NONE) {
		pr_err("Failed to get Watchdog start register address\n");
		return -EINVAL;
	}

	dev->priv_ = priv;

	return 0;
}

static struct wdt_ops spacemit_wdt_ops = {
	.start = spacemit_wdt_start,
	.stop = spacemit_wdt_stop,
	.reset = spacemit_wdt_reset,
	.expire_now = spacemit_wdt_expire_now,
};

static const struct udevice_id spacemit_wdt_ids[] = {
	{ .compatible = "spacemit,k1x-wdt" },
	{ /* sentinel */ }
};

U_BOOT_DRIVER(spacemit_wdt) = {
	.name = "spacemit_wdt",
	.id = UCLASS_WDT,
	.of_match = spacemit_wdt_ids,
	.probe = spacemit_wdt_probe,
	.ops = &spacemit_wdt_ops,
	.remove = spacemit_wdt_remove,
	.flags  = DM_FLAG_OS_PREPARE,
};
