// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/pwm/pwm-pxa.c
 *
 * simple driver for PWM (Pulse Width Modulator) controller
 */

#include <common.h>
#include <clk.h>
#include <reset.h>
#include <dm.h>
#include <pwm.h>
#include <div64.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <div64.h>

/* PWM registers and bits definitions */
#define PWMCR		(0x00)
#define PWMDCR		(0x04)
#define PWMPCR		(0x08)

#define PWMCR_SD	(1 << 6)
#define PWMDCR_FD	(1 << 10)

struct pxa_pwm_priv {
	struct clk	clk;
	struct reset_ctl	reset;
	void __iomem	*mmio_base;
	unsigned int num_pwms;
	int dcr_fd; /* Controller PWM_DCR FD feature */
};

/*
 * period_ns = 10^9 * (PRESCALE + 1) * (PV + 1) / PWM_CLK_RATE
 * duty_ns   = 10^9 * (PRESCALE + 1) * DC / PWM_CLK_RATE
 */
static int pxa_pwm_set_config(struct udevice *dev, uint channel,
				uint period_ns, uint duty_ns)
{
	struct pxa_pwm_priv *priv = dev_get_priv(dev);
	unsigned long long c;
	unsigned long period_cycles, prescale, pv, dc;
	int rc;

	c = clk_get_rate(&priv->clk);
	c = c * period_ns;
	do_div(c, 1000000000);
	period_cycles = c;

	if (period_cycles < 1)
		period_cycles = 1;
	prescale = (period_cycles - 1) / 1024;
	pv = period_cycles / (prescale + 1) - 1;

	if (prescale > 63)
		return -EINVAL;

	if (duty_ns == period_ns) {
		if(priv->dcr_fd)
			dc = PWMDCR_FD;
		else{
			dc = (pv + 1) * duty_ns / period_ns;
			if (dc >= PWMDCR_FD) {
				dc = PWMDCR_FD - 1;
				pv = dc - 1;
			}
		}
	} else
		dc = (pv + 1) * duty_ns / period_ns;

	/*
	 * FIXME: Graceful shutdown mode would cause the function clock
	 * could not be enabled normally, so chose abrupt shutdown mode.
	 */
	prescale |= PWMCR_SD;

	/* NOTE: the clock to PWM has to be enabled first
	 * before writing to the registers
	 */
	rc = clk_enable(&priv->clk);
	if (rc < 0)
		return rc;

	writel(prescale, priv->mmio_base + PWMCR);
	writel(pv, priv->mmio_base + PWMPCR);
	writel(dc, priv->mmio_base + PWMDCR);

	clk_disable(&priv->clk);
	rc = clk_enable(&priv->clk);
	return 0;
}

static int pxa_pwm_set_enable(struct udevice *dev, uint channel, bool enable)
{
	struct pxa_pwm_priv *priv = dev_get_priv(dev);

	if (enable) {
		return clk_enable(&priv->clk);
	}
	else
		return clk_disable(&priv->clk);
}

static const struct pwm_ops pxa_pwm_ops = {
	.set_config	= pxa_pwm_set_config,
	.set_enable	= pxa_pwm_set_enable,
};

static int pxa_pwm_probe(struct udevice *dev)
{
	struct pxa_pwm_priv *priv = dev_get_priv(dev);
	int ret = 0;

	priv->dcr_fd = dev_read_u32_default(dev, "k1x,pwm-disable-fd", 0);

	ret = clk_get_by_index(dev,0,&priv->clk);
	if(ret)
		return ret;

	ret = reset_get_by_index(dev, 0, &priv->reset);
	if (ret)
		return ret;
	ret = reset_deassert(&priv->reset);
	if (ret) {
		pr_err("Failed to de-assert reset for PWM (error %d)\n",ret);
		goto err_rst;
	}

	priv->num_pwms = 1;

	priv->mmio_base = dev_read_addr_ptr(dev);
	if (!priv->mmio_base)
		return -EINVAL;

	return 0;

err_rst:
	reset_assert(&priv->reset);
	return ret;
}

static const struct udevice_id pxa_pwm_ids[] = {
	{ .compatible = "spacemit,k1x-pwm", .data = 0 },
	{ }
};

U_BOOT_DRIVER(spacemit_pwm) = {
	.name = "pxa_pwm",
	.id = UCLASS_PWM,
	.of_match = pxa_pwm_ids,
	.ops = &pxa_pwm_ops,
	.probe = pxa_pwm_probe,
	.priv_auto	= sizeof(struct pxa_pwm_priv),
};
