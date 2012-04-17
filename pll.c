/*
 * PLL access (AD9516) for fine-delay card 
 *
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2 as published by the Free Software Foundation or, at your
 * option, any later version.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include "fine-delay.h"
#include "hw/pll_config.h" /* the table to be written */

static int pll_writel(struct spec_fd *fd, int val, int reg)
{
	return fd_spi_xfer(fd, FD_CS_PLL, 24, (reg << 8) | val, NULL);
}

static int pll_readl(struct spec_fd *fd, int reg)
{
	uint32_t ret;
	int err;

	err = fd_spi_xfer(fd, FD_CS_PLL, 24, (reg << 8) | (1 << 23), &ret);
	if (err < 0)
		return err;
	return ret & 0xff;
}

int fd_pll_init(struct spec_fd *fd)
{
	int i;
	unsigned long j;
	const struct ad9516_reg *r;

	pr_debug("%s\n",__func__);
	if (pll_writel(fd, 0x99, 0x000) < 0)
		goto out;
	if (pll_writel(fd, 0x01, 0x232) < 0)
		goto out;
	i = pll_readl(fd, 0x003);
	if (i < 0)
		goto out;
	if (i != 0xc3) {
		pr_err("%s: Error in PLL communication\n", KBUILD_MODNAME);
		pr_err("   (got 0x%x, expected 0xc3)\n", i);
		return -EIO;
	}

	/* Write the magic config */
	for (i = 0, r = __9516_regs; i < ARRAY_SIZE(__9516_regs); i++, r++) {
		if (pll_writel(fd, r->val, r->reg) < 0) {
			pr_err("%s: Error in configuring PLL (step %i)\n",
			       KBUILD_MODNAME, i);
			return -EIO;
		}
	}

	if (pll_writel(fd, 0x01, 0x232) < 0)
		goto out;

	/* Wait for it to lock */
	j = jiffies + HZ / 2;
	while (jiffies < j) {
		i = pll_readl(fd, 0x1f);
		if (i < 0)
			return -EIO;
		if (i & 1)
			break;
		msleep(1);
	}
	if (!(i & 1))
		return -ETIMEDOUT;

	/*
	 * Synchronize the phase of all clock outputs
	 * (this is critical for the accuracy!)
	 */

	if (pll_writel(fd, 0x01, 0x230) < 0)
		goto out;
	if (pll_writel(fd, 0x01, 0x232) < 0)
		goto out;
	if (pll_writel(fd, 0x00, 0x230) < 0)
		goto out;
	if (pll_writel(fd, 0x01, 0x232) < 0)
		goto out;

	pr_debug("success!\n");
	return 0;



out:
	pr_err("%s: Error in SPI communication\n", KBUILD_MODNAME);
	return -EIO;
}

void fd_pll_exit(struct spec_fd *fd)
{
	/* nothing to do */
}

