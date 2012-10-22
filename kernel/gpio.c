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

#define SPI_RETRIES 100

static int gpio_writel(struct fd_dev *fd, int val, int reg)
{
	int rval = fd_spi_xfer(fd, FD_CS_GPIO, 24,
			   0x4e0000 | (reg << 8) | val, NULL);

	fd_spi_xfer(fd, FD_CS_NONE, 24,
			  0, NULL);

	return rval;
}

static int gpio_readl(struct fd_dev *fd, int reg)
{
	uint32_t ret;
	int err;

	err = fd_spi_xfer(fd, FD_CS_GPIO, 24,
			  0x4f0000 | (reg << 8), &ret);

	fd_spi_xfer(fd, FD_CS_NONE, 24,
			  0, NULL);

	if (err < 0)
		return err;
	return ret & 0xff;
}

static int gpio_writel_with_retry(struct fd_dev *fd, int val, int reg)
{
	int retries = SPI_RETRIES, rv;
	while(retries--)
	{
		gpio_writel(fd, val, reg);
		rv = gpio_readl(fd, reg);
		if(rv >= 0 && (rv == val))
		{
			if(SPI_RETRIES-1-retries > 0)
				pr_info("%s: succeded after %d retries\n",
				       __func__, SPI_RETRIES - 1 - retries);
			return 0;
		}
	}
	return -EIO;
}

void fd_gpio_dir(struct fd_dev *fd, int mask, int dir)
{
	fd->mcp_iodir &= ~mask;
	if (dir == FD_GPIO_IN)
		fd->mcp_iodir |= mask;

	gpio_writel_with_retry(fd, (fd->mcp_iodir & 0xff), FD_MCP_IODIR);
	gpio_writel_with_retry(fd, (fd->mcp_iodir >> 8), FD_MCP_IODIR+1);
}

void fd_gpio_val(struct fd_dev *fd, int mask, int values)
{

	fd->mcp_olat &= ~mask;
	fd->mcp_olat |= values;

	gpio_writel_with_retry(fd, (fd->mcp_olat & 0xff), FD_MCP_OLAT);
	gpio_writel_with_retry(fd, (fd->mcp_olat >> 8), FD_MCP_OLAT+1);
}

void fd_gpio_set_clr(struct fd_dev *fd, int mask, int set)
{
	if (set)
		fd_gpio_val(fd, mask, mask);
	else
		fd_gpio_val(fd, mask, 0);
}

int fd_gpio_init(struct fd_dev *fd)
{
	int i, val;

	fd->mcp_iodir = 0xffff;
	fd->mcp_olat = 0;

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

void fd_gpio_exit(struct fd_dev *fd)
{
	/* nothing to do */
}

int fd_dump_mcp(struct fd_dev *fd)
{
	printk(KERN_DEBUG "MCP23S17 register dump\n");
	printk(KERN_DEBUG "IOCON: 0x%02x\n", gpio_readl(fd, FD_MCP_IOCON));
	printk(KERN_DEBUG "IODIRA: 0x%02x\n", gpio_readl(fd, FD_MCP_IODIR));
	printk(KERN_DEBUG "IODIRB: 0x%02x\n", gpio_readl(fd, FD_MCP_IODIR+1));
	printk(KERN_DEBUG "OLATA: 0x%02x\n", gpio_readl(fd, FD_MCP_OLAT));
	printk(KERN_DEBUG "OLATB: 0x%02x\n", gpio_readl(fd, FD_MCP_OLAT+1));
	return 0;
}
