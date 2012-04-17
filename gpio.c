/*
 * SPI access to fine-delay internals
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

static int gpio_writel(struct spec_fd *fd, int val, int reg)
{
	return fd_spi_xfer(fd, FD_CS_GPIO, 24,
			   0x4e0000 | (reg << 8) | val, NULL);
}

static int gpio_readl(struct spec_fd *fd, int reg)
{
	uint32_t ret;
	int err;

	err = fd_spi_xfer(fd, FD_CS_GPIO, 24,
			  0x4f0000 | (reg << 8), &ret);
	if (err < 0)
		return err;
	return ret & 0xff;
}

void fd_gpio_dir(struct spec_fd *fd, int mask, int dir)
{
	int addr, val;

	/* if mask is bits 8..15 use the next address */
	addr = FD_MCP_IODIR;
	if (mask & 0xff00) {
		mask >>= 8;
		addr++;
	}
	val = gpio_readl(fd, addr) & ~mask;
	if (dir == FD_GPIO_IN)
		val |= mask;
	gpio_writel(fd, val, addr);
}

void fd_gpio_val(struct spec_fd *fd, int mask, int values)
{
	int addr, reg;

	/* if mask is bits 8..15 use the next address */
	addr = FD_MCP_OLAT;
	if (mask & 0xff00) {
		mask >>= 8;
		values >>= 8;
		addr++;
	}
	reg = gpio_readl(fd, addr) & ~mask;
	gpio_writel(fd, reg | values, addr);
}


int fd_gpio_init(struct spec_fd *fd)
{
	int i, val;

	pr_debug("%s\n",__func__);
	if (gpio_writel(fd, 0x00, FD_MCP_IOCON) < 0)
		goto out;

	/* Try to read and write a register to test the SPI connection */
	for (val = 0xaa; val >= 0; val -= 0x11) {
		if (gpio_writel(fd, val, FD_MCP_IPOL) < 0)
			goto out;
		i = gpio_readl(fd, FD_MCP_IPOL);
		if (i < 0)
			goto out;
		if (i != val) {
			pr_err("%s: Error in GPIO communication\n",
			       KBUILD_MODNAME);
			pr_err("   (got 0x%x, expected 0x%x)\n", i, val);
			return -EIO;
		}
	}
	/* last time we wrote 0, ok */
	return 0;
out:
	pr_err("%s: Error in SPI communication\n", KBUILD_MODNAME);
	return -EIO;
}

void fd_gpio_exit(struct spec_fd *fd)
{
	/* nothing to do */
}

