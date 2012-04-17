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

#include <linux/io.h>
#include "fine-delay.h"

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
	int reg;

	pr_debug("%s\n",__func__);
	if (pll_writel(fd, 0x99, 0x000) < 0)
		goto out;
	if (pll_writel(fd, 0x01, 0x232) < 0)
		goto out;
	reg = pll_readl(fd, 0x003);
	if (reg < 0)
		goto out;
	if (reg != 0xc3) {
		pr_err("%s: Error in PLL communication\n", KBUILD_MODNAME);
		pr_err("   (got 0x%x, expected 0xc3)\n", reg);
		return -EIO;
	}

	/* FIXME: program the pll */

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

