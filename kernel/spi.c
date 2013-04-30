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

#include <linux/jiffies.h>
#include <linux/io.h>
#include <linux/delay.h>
#include "fine-delay.h"
#include "hw/fd_main_regs.h"

int fd_spi_xfer(struct fd_dev *fd, int ss, int num_bits,
		uint32_t in, uint32_t *out)
{
	uint32_t scr = 0, r;
	unsigned long j = jiffies + HZ;

	scr = FD_SCR_DATA_W(in)| FD_SCR_CPOL;
	if(ss == FD_CS_PLL)
		scr |= FD_SCR_SEL_PLL;
	else if(ss == FD_CS_GPIO)
		scr |= FD_SCR_SEL_GPIO;

	fd_writel(fd, scr, FD_REG_SCR);
	fd_writel(fd, scr | FD_SCR_START, FD_REG_SCR);
	while (!(fd_readl(fd, FD_REG_SCR) & FD_SCR_READY))
		if (jiffies > j)
			break;
	if (!(fd_readl(fd, FD_REG_SCR) & FD_SCR_READY))
		return -EIO;
	scr = fd_readl(fd, FD_REG_SCR);
	r = FD_SCR_DATA_R(scr);
	if(out) *out=r;
	udelay(100); /* FIXME: check */
	return 0;
}


int fd_spi_init(struct fd_dev *fd)
{
	/* write default to DAC for VCXO */
	fd_spi_xfer(fd, FD_CS_DAC, 24, fd->calib.vcxo_default_tune & 0xffff,
		    NULL);
	return 0;
}

void fd_spi_exit(struct fd_dev *fd)
{
	/* nothing to do */
}

