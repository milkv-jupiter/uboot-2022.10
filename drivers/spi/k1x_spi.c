// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Spacemit k1x spi controller
 *
 * Copyright (c) 2023, spacemit Corporation.
 *
 */

#include <common.h>
#include <malloc.h>
#include <dm.h>
#include <spi.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <clk.h>
#include <reset.h>
#include <asm/gpio.h>
#include "k1x_spi.h"

#define TIMEOUT 		100000

#define SSP_DATA_16BIT		16
#define SSP_DATA_8BIT		8
#define SSP_DATA_32BIT		32
#define SSP_DATA_18BIT		18

#define SSP_CLK_26		26000000
#define SSP_CLK_6P5		6500000

#define K1X_APBC_BASE   0xd4015000
#define K1X_APBC2_BASE  0xf0610000
#define K1X_GPIO_BASE	0xd4019000
#define APBC_SSPA0		0x80	/* Clock/Reset Control Register for SSPA0 */
#define APBC_SSPA1		0x84	/* Clock/Reset Control Register for SSPA1 */
#define APBC_SSP2		0x4	    /* Clock/Reset Control Register for SSPA1 */
#define APBC_SSP3		0x7C	/* Clock/Reset Control Register for SSP3 */
#define K1X_APBC_ADDR(x)		((uint32_t)((K1X_APBC_BASE) + (x)))
#define REG_APBC_SSPA0_CLK_RST	K1X_APBC_ADDR(APBC_SSPA0) /*Clock/Reset Control Register for SSPA 0*/
#define REG_APBC_SSPA1_CLK_RST	K1X_APBC_ADDR(APBC_SSPA1) /*Clock/Reset Control Register for SSPA 1*/
#define REG_APBC_SSP2_CLK_RST	((uint32_t)(K1X_APBC2_BASE + APBC_SSP2)) /*Clock/Reset Control Register for SSPA 1*/
#define REG_APBC_SSP3_CLK_RST	K1X_APBC_ADDR(APBC_SSP3)  /*Clock/Reset Control Register for SSP 3*/

/* Common APB clock register bit definitions */
#define APBC_APBCLK		(1<<0)  /* APB Bus Clock Enable */
#define APBC_FNCLK		(1<<1)  /* Functional Clock Enable */
#define APBC_RST		(1<<2)  /* Reset Generation */

/* APB functional Clock Selection Mask */
#define APBC_FNCLKSEL(x)        (((x) & 0x7) << 4)

#define SSP_DATA_16_BIT		(15 << 5)
#define SSP_DATA_8_BIT		(7 << 5)
#define SSP_DATA_32_BIT		(31 << 5)
#define SSP_DATA_18_BIT		(17 << 5)

struct k1x_spi {
	void __iomem *base;
	struct clk clk;
	struct reset_ctl reset;
	unsigned int speed;
	unsigned int freq;
	unsigned int mode;
	unsigned int port;
	unsigned int cs_gpio;
	int master;
	int flags;
	int n_bytes;
	int (*write)(struct k1x_spi *priv);
	int (*read)(struct k1x_spi *priv);
	int data_length;
	void *tx;
	void *tx_end;
	void *rx;
	void *rx_end;
	int len;
	int dma;
};

static bool k1x_spi_txfifo_full(struct k1x_spi *priv)
{
	return !(readl(priv->base + REG_SSP_STATUS) & BIT_SSP_TNF);
}

int k1x_spi_flush(struct k1x_spi *priv)
{
	unsigned long limit = 1 << 13;

	do {
		while (readl(priv->base + REG_SSP_STATUS) & BIT_SSP_RNE)
			readl(priv->base + REG_SSP_DATAR);
	} while ((readl(priv->base + REG_SSP_STATUS) & BIT_SSP_BSY) && --limit);
	writel(BIT_SSP_ROR, priv->base + REG_SSP_STATUS);

	return limit;
}

static int null_writer(struct k1x_spi *priv)
{
	u8 n_bytes = priv->n_bytes;

	if (k1x_spi_txfifo_full(priv)
		|| (priv->tx == priv->tx_end))
		return 0;

	writel(0, priv->base + REG_SSP_DATAR);
	priv->tx += n_bytes;

	return 1;
}

static int null_reader(struct k1x_spi *priv)
{
	u8 n_bytes = priv->n_bytes;

	while ((readl(priv->base + REG_SSP_STATUS) & BIT_SSP_RNE)
	       && (priv->rx < priv->rx_end)) {
		readl(priv->base + REG_SSP_DATAR);
		priv->rx += n_bytes;
	}

	return priv->rx == priv->rx_end;
}

static int u8_writer(struct k1x_spi *priv)
{
	if (k1x_spi_txfifo_full(priv)
		|| (priv->tx == priv->tx_end))
		return 0;

	writel(*(u8 *)(priv->tx), priv->base + REG_SSP_DATAR);
	++priv->tx;

	return 1;
}

static int u8_reader(struct k1x_spi *priv)
{
	while ((readl(priv->base + REG_SSP_STATUS) & BIT_SSP_RNE)
	       && (priv->rx < priv->rx_end)) {
		*(u8 *)(priv->rx) = readl(priv->base + REG_SSP_DATAR);
		++priv->rx;
	}

	return priv->rx == priv->rx_end;
}

void k1x_spi_set_selfloop(struct k1x_spi *priv, int is_loop)
{
	if (is_loop)
		writel(BIT_SSP_LBM | readl(REG_SSP_TOP_CTRL + priv->base),
				REG_SSP_TOP_CTRL + priv->base);
	else
		writel((~BIT_SSP_LBM) & readl(REG_SSP_TOP_CTRL + priv->base),
				REG_SSP_TOP_CTRL + priv->base);
}

static void k1x_gpio_set_output(int gpio, int value)
{
	void __iomem *reg;
	u32 reg_gbase = 0, reg_goff = 0, bit_no = 0;
	u32 val;

	if (gpio < 96) {
		reg_goff = (gpio >> 5) * (0x4);
	} else {
		reg_goff = 0x100;
	}
	reg_gbase = K1X_GPIO_BASE + reg_goff;
	bit_no = (gpio) & 0x1f;

	reg =  (void *)(ulong)(reg_gbase + 0xc);
	val = readl(reg);
	val |= 1 << bit_no;
	writel(val, reg);

	udelay(2);

	if (value) {
		reg =  (void *)(ulong)(reg_gbase + 0x18);
		val = readl(reg);
		val |= 1 << bit_no;
		writel(val, reg);
	} else {
		reg =  (void *)(ulong)(reg_gbase + 0x24);
		val = readl(reg);
		val |= 1 << bit_no;
		writel(val, reg);
	}
}

static void k1x_gpio_set_value(int gpio, int value)
{
	void __iomem *reg;
	u32 reg_gbase = 0, reg_goff = 0, bit_no = 0;
	u32 val;

	if (gpio < 96) {
		reg_goff = (gpio >> 5) * (0x4);
	} else {
		reg_goff = 0x100;
	}
	reg_gbase = K1X_GPIO_BASE + reg_goff;
	bit_no = (gpio) & 0x1f;

	if (value) {
		reg =  (void *)(ulong)(reg_gbase + 0x18);
		val = readl(reg);
		val |= 1 << bit_no;
		writel(val, reg);
	} else {
		reg =  (void *)(ulong)(reg_gbase + 0x24);
		val = readl(reg);
		val |= 1 << bit_no;
		writel(val, reg);
	}
}

static void k1x_stop_ssp(struct k1x_spi *priv)
{
	writel(BIT_SSP_ROR | BIT_SSP_TINT, priv->base + REG_SSP_STATUS);
	writel(BITS_SSP_RFT(8) | BITS_SSP_TFT(7), priv->base + REG_SSP_FIFO_CTRL);
	writel(0, priv->base + REG_SSP_TO);
}

int k1x_spi_pio_xfer(struct k1x_spi *priv, int len, void *din, unsigned long flags)
{
	if (flags & SPI_XFER_BEGIN) {
		k1x_gpio_set_value(priv->cs_gpio, 0);
	}

	do {
		if (priv->read(priv)) {
			k1x_stop_ssp(priv);
			break;
		}
	} while (priv->write(priv));

	if (flags & SPI_XFER_END) {
		k1x_gpio_set_value(priv->cs_gpio, 1);
	}

	din = priv->rx;
	return 0;
}

static int k1x_spi_transfer_config(struct k1x_spi *priv)
{
	uint32_t sscr0 = 0;
	u32 top_ctrl;
	u32 fifo_ctrl;

	priv->n_bytes = 1;

	/* empty read buffer */
	if (k1x_spi_flush(priv) == 0) {
		printf("k1x spi flush failed\n");
		return -1;
	}

	top_ctrl = readl(priv->base + REG_SSP_TOP_CTRL);
	fifo_ctrl = readl(priv->base + REG_SSP_FIFO_CTRL);

	switch (priv->mode) {
	case SPI_MODE_0:
		break;
	case SPI_MODE_1:
		sscr0 |= BIT_SSP_SPH;
		break;
	case SPI_MODE_2:
		sscr0 |= BIT_SSP_SPO;
		break;
	case SPI_MODE_3:
		sscr0 |= BIT_SSP_SPO | BIT_SSP_SPH;
		break;
	default:
		return -1;
	}
	top_ctrl |= sscr0;

	priv->speed = 0x0;
	switch (priv->data_length) {
	case 8:
		priv->speed |= SSP_DATA_8_BIT;
		break;
	case 18:
		priv->speed |= SSP_DATA_18_BIT;
		break;
	case 32:
		priv->speed |= SSP_DATA_32_BIT;
		break;
	case 16:
	default:
		priv->speed |= SSP_DATA_16_BIT;
	}
	top_ctrl |= priv->speed;

	writel(BIT_SSP_ROR | BIT_SSP_TINT, priv->base + REG_SSP_STATUS);
	writel(0xbb8, priv->base + REG_SSP_TO);

	top_ctrl |= BIT_SSP_HOLD_FRAME_LOW;

	top_ctrl &= ~BIT_SSP_SSE;
	writel(top_ctrl, priv->base + REG_SSP_TOP_CTRL);
	writel(fifo_ctrl, priv->base + REG_SSP_FIFO_CTRL);
	top_ctrl |= BIT_SSP_SSE;

	writel(top_ctrl, priv->base + REG_SSP_TOP_CTRL);

	return 0;
}

static int k1x_spi_claim_bus(struct udevice *dev)
{
	return 0;
}

static int k1x_spi_release_bus(struct udevice *dev)
{
	u32 val = 0;
	struct udevice *bus = dev->parent;
	struct k1x_spi *priv = dev_get_priv(bus);
	printf("k1x spi release bus\n");

	val = readl(priv->base + REG_SSP_TOP_CTRL);
	val &= ~(BIT_SSP_SSE | BIT_SSP_HOLD_FRAME_LOW);
	writel(val, priv->base + REG_SSP_TOP_CTRL);
	return 0;
}

static int k1x_spi_set_mode(struct udevice *bus, uint mode)
{
	struct k1x_spi *priv = dev_get_priv(bus);

	printf("k1x spi set mode = %d.\n", mode);
	priv->mode = mode;

	return 0;
}

static int k1x_spi_set_speed(struct udevice *bus, uint speed)
{
	struct k1x_spi *priv = dev_get_priv(bus);

	printf("k1x spi set speed = %d\n", speed);

	if (priv->freq != speed) {
		priv->freq = speed;
		clk_set_rate(&priv->clk, priv->freq);
	}

	return 0;
}

static int k1x_spi_xfer(struct udevice *dev, unsigned int bitlen,
			   const void *dout, void *din, unsigned long flags)
{
	struct udevice *bus = dev->parent;
	struct k1x_spi *priv = dev_get_priv(bus);
	int ret;

	priv->len = bitlen >> 3;

	priv->tx = (void *)dout;
	priv->tx_end = priv->tx + priv->len;
	priv->rx = din;
	priv->rx_end = priv->rx + priv->len;
	if (priv->tx) {
		priv->write = u8_writer;
	} else {
		priv->write = null_writer;
	}
	if (priv->rx) {
		priv->read = u8_reader;
	} else {
		priv->read = null_reader;
	}

	k1x_spi_transfer_config(priv);
	ret = k1x_spi_pio_xfer(priv, priv->len, din, flags);

	return ret;
}

static int k1x_spi_probe(struct udevice *dev)
{
	struct k1x_spi *priv = dev_get_priv(dev);
	int ret;

	priv->base = dev_remap_addr(dev);
	if (!priv->base)
		return -EINVAL;

	priv->freq = dev_read_u32_default(dev, "clock-frequency", 0);
	if (!priv->freq) {
		printf("Please provide clock-frequency!\n");
		return -EINVAL;
	}

	dev_read_u32(dev, "port", &priv->port);
	if ((priv->port < 0) || (priv->port > 4)) {
		printf("Please provide valid port num!\n");
		return -EINVAL;
	}

	dev_read_u32(dev, "cs-gpio", &priv->cs_gpio);
	printf("cs gpio %d\n", priv->cs_gpio);
	if ((priv->cs_gpio < 0) || (priv->cs_gpio > 128)) {
		printf("Please provide valid gpio num!\n");
		return -EINVAL;
	}

	ret = clk_get_by_index(dev, 0, &priv->clk);
	if (ret) {
		pr_err("It has no clk: %d\n", ret);
		return ret;
	}

	ret = reset_get_by_index(dev, 0, &priv->reset);
	if (ret) {
		pr_err("It has no reset: %d\n", ret);
		return ret;
	}

	k1x_gpio_set_output(priv->cs_gpio, 1);

	/* default clk */
	clk_set_rate(&priv->clk, priv->freq);
	clk_enable(&priv->clk);
	reset_deassert(&priv->reset);

	/* current default settings */
	writel(0, priv->base + REG_SSP_TOP_CTRL);
	writel(0, priv->base + REG_SSP_FIFO_CTRL);
	writel(BITS_SSP_RFT(8) | BITS_SSP_TFT(7), priv->base + REG_SSP_FIFO_CTRL);
	writel(SSP_DATA_8_BIT, priv->base + REG_SSP_TOP_CTRL);
	priv->dma = 0;
	priv->data_length = SSP_DATA_8BIT;
	writel(0, priv->base + REG_SSP_TO);
	writel(0, priv->base + REG_SSP_PSP_CTRL);

	/* loopback test need the following setting */
	//k1x_spi_set_selfloop(priv, 1);

	return 0;
}

static const struct dm_spi_ops k1x_spi_ops = {
	.claim_bus	= k1x_spi_claim_bus,
	.release_bus	= k1x_spi_release_bus,
	.set_mode = k1x_spi_set_mode,
	.set_speed = k1x_spi_set_speed,
	.xfer = k1x_spi_xfer,
};

static const struct udevice_id k1x_spi_ids[] = {
	{ .compatible = "spacemit,k1x-spi" },
	{ }
};

U_BOOT_DRIVER(k1x_spi) = {
	.name = "k1x_spi",
	.id = UCLASS_SPI,
	.of_match = k1x_spi_ids,
	.ops = &k1x_spi_ops,
	.priv_auto = sizeof(struct k1x_spi),
	.probe = k1x_spi_probe,
};
