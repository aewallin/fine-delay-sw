/*
 * Accessing the ACAM chip and configuring it.
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
#include "hw/acam_gpx.h"

/*
 * Calculation is fixed point: picoseconds and 16 decimals (i.e. ps << 16).
 * We know the bin is small, but the Tref is several nanos so we need 64 bits
 * (although our current values fit in 32 bits after the division)
 */
#define ACAM_FP_BIN	((int)(ACAM_DESIRED_BIN * (1 << 16)))
#define ACAM_FP_TREF	(((1000LL * 1000 * 1000) << 16) / ACAM_CLOCK_FREQ_KHZ)

static int acam_calc_pll(uint64_t tref, int bin, int *hsdiv_out,
			 int *refdiv_out)
{
	int x, refdiv, hsdiv;

	/*
	 *     Tbin(I-mode) = (Tref << refdiv) / (216 * hsdiv)
	 *
	 * so, calling X the value "hsdiv >> refdiv" we have
	 *
	 *     X = Tref / (216 * Tbin)
	 *
	 * Then, we can choose refdiv == 7 to have the best bits,
	 * and then shift out the zeros to get smaller values.
	 * 
	 */
	x = (tref << 16) / 216 / bin;
	//printf("x = %lf\n", (double)x / (1<<16));

	/* Now, shift out the max bits (usually 7) and drop decimal part */
	refdiv = ACAM_MAX_REFDIV;
	hsdiv = (x << refdiv) >> 16;
	/* Check the first decimal bit and approximate */
	if ((x << refdiv) & (1 << 15))
		hsdiv++;

	/* until we have zeroes as LSB, shift out to decrease pll quotient */
	while (refdiv > 0 && !(hsdiv & 1)) {
		refdiv--;
		hsdiv >>= 1;
	}
	*hsdiv_out = hsdiv;
	*refdiv_out = refdiv;

	/* Finally, calculate what we really have */
	return (tref << refdiv) / 216 / hsdiv;
}

static void acam_set_address(struct spec_fd *fd, int addr)
{
	if (addr == fd->acam_addr)
		return;
	if (fd->acam_addr == -1) {
		/* first time */
		fd_gpio_dir(fd, 0xf00, FD_GPIO_OUT);
	}
	fd_gpio_val(fd, 0xf00, addr << 8);
	fd->acam_addr = addr;
}

/* Warning: acam_readl and acam_writel only work if GCR.BYPASS is set */
static uint32_t acam_readl(struct spec_fd *fd, int reg)
{
	acam_set_address(fd, reg);
	writel(FD_TDCSR_READ, fd->regs + FD_REG_TDCSR);
	return readl(fd->regs + FD_REG_TDR) & ACAM_MASK;
}

static void acam_writel(struct spec_fd *fd, int val, int reg)
{
	writel(val, fd->regs + FD_REG_TDR);
	writel(FD_TDCSR_WRITE, fd->regs + FD_REG_TDCSR);
}

static inline int acam_is_pll_locked(struct spec_fd *fd)
{
        return !(acam_readl(fd, 12) &AR12_NotLocked);
}

/* Two test functions to verify the bus is working -- Tom */
static int acam_test_addr_bit(struct spec_fd *fd, int base, int bit,
			       int data)
{
	int addr1 = base;
	int addr2 = base + (1<<bit);
	int reg;

	reg = acam_readl(fd, addr1) & ~data;
	acam_writel(fd, reg, addr1); /* zero the data mask */
	reg = acam_readl(fd, addr2) | data;
	acam_writel(fd, reg, addr2); /* set the data mask */

	if ((acam_readl(fd, addr1) & data) != 0)
		return -EIO;
	if ((acam_readl(fd, addr2) & data) != data)
		return -EIO;

	/* the other way around */
	reg = acam_readl(fd, addr2) & ~data;
	acam_writel(fd, reg, addr2); /* zero the data mask */
	reg = acam_readl(fd, addr1) | data;
	acam_writel(fd, reg, addr1); /* set the data mask */

	if ((acam_readl(fd, addr2) & data) != 0)
		goto out;
	if ((acam_readl(fd, addr1) & data) != data)
		goto out;
	return 0;

out:
	pr_err("%s: ACAM address bit %i failure\n", KBUILD_MODNAME, bit);
	return -EIO;
}

static int acam_test_bus(struct spec_fd *fd)
{
	int err = 0, i, v;

	/* Use register 5 to checke the data bits */
	for(i = 0; i & ACAM_MASK; i <<= 1) {
		acam_writel(fd, i, 5);
		acam_readl(fd, 0);
		v = acam_readl(fd, 5);
		if (v != i)
			goto out;

		acam_writel(fd, ~i & ACAM_MASK, 5);
		acam_readl(fd, 0);
		v = acam_readl(fd, 5);
		if (v != (~i & ACAM_MASK))
			goto out;
	}
	err += acam_test_addr_bit(fd, 0, 0, 0x000001);
	err += acam_test_addr_bit(fd, 1, 1, 0x000008);
	err += acam_test_addr_bit(fd, 0, 2, 0x000001);
	err += acam_test_addr_bit(fd, 3, 3, 0x010000);
	if (err)
		return -EIO;
	return 0;

out:
	pr_err("%s: ACAM data bit 0x%06x failure\n", KBUILD_MODNAME, i);
	return -EIO;
}



int fd_acam_init(struct spec_fd *fd)
{
	fd->acam_addr = -1; /* First time must be activated */
	return 0;
}

void fd_acam_exit(struct spec_fd *fd)
{
	/* nothing to do */
}

