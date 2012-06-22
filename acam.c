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
//#include <linux/math64.h>
#include <linux/moduleparam.h>
#include "fine-delay.h"
#include "hw/fd_main_regs.h"
#include "hw/acam_gpx.h"

int fd_calib_period_s = 30;
module_param_named(calib_s, fd_calib_period_s, int, 0444);

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
	uint64_t tmpll;
	int x, refdiv, hsdiv;
	int32_t rem;

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
	if (0) {
		x = (tref << 16) / 216 / bin;
		//printf("x = %lf\n", (double)x / (1<<16));
	} else {
		/* We can't divide 64 bits in kernel space */
		tmpll = div_u64_rem(tref << 16, 216, &rem);
		x = div_u64_rem(tmpll, bin, &rem);
	}

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
	if (0) {
		bin = (tref << refdiv) / 216 / hsdiv;
	} else {
		tmpll = div_u64_rem(tref << refdiv, 216, &rem);
		bin = div_u64_rem(tmpll, hsdiv, &rem);
	}
	return (bin + 1) / 3; /* We are in I-Mode, R-Mode bin is 1/3 if this */
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
uint32_t acam_readl(struct spec_fd *fd, int reg)
{
	acam_set_address(fd, reg);
	fd_writel(fd, FD_TDCSR_READ, FD_REG_TDCSR);
	return fd_readl(fd, FD_REG_TDR) & ACAM_MASK;
}

void acam_writel(struct spec_fd *fd, int val, int reg)
{
	acam_set_address(fd, reg);
	fd_writel(fd, val, FD_REG_TDR);
	fd_writel(fd, FD_TDCSR_WRITE, FD_REG_TDCSR);
}

static void acam_set_bypass(struct spec_fd *fd, int on)
{
	/* FIXME: this zeroes all other GCR bits */
	fd_writel(fd, on ? FD_GCR_BYPASS : 0, FD_REG_GCR);
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
		goto out;
	if ((acam_readl(fd, addr2) & data) != data)
		goto out;

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


/* We need to write come static configuration in the registers */
struct acam_init_data {
	int addr;
	int val;
};

/* Commented values are not constant, they are added at runtime (see later) */
static struct acam_init_data acam_init_rmode[] = {
	{0,	AR0_ROsc | AR0_RiseEn0 | AR0_RiseEn1 | AR0_HQSel},
	{1,	AR1_Adj(0, 0) | AR1_Adj(1, 2) |	AR1_Adj(2, 6) |
		AR1_Adj(3, 0) |	AR1_Adj(4, 2) | AR1_Adj(5, 6) | AR1_Adj(6, 0)},
	{2,	AR2_RMode | AR2_Adj(7, 2) | AR2_Adj(8, 6)},
	{3,	0},
	{4,	AR4_EFlagHiZN},
	{5,	AR5_StartRetrig
		| 0 /* AR5_StartOff1(hw->calib.acam_start_offset) */
		| AR5_MasterAluTrig},
	{6,	AR6_Fill(200) | AR6_PowerOnECL},
	{7,	/* AR7_HSDiv(hsdiv) | AR7_RefClkDiv(refdiv) */ 0
		| AR7_ResAdj | AR7_NegPhase},
	{11,	0x7ff0000},
	{12,	0x0000000},
	{14,	0},
	/* finally, reset */
	{4,	AR4_EFlagHiZN | AR4_MasterReset | AR4_StartTimer(0)},
};

static struct acam_init_data acam_init_imode[] = {
	{0,	AR0_TRiseEn(0) | AR0_HQSel | AR0_ROsc},
	{2,	AR2_IMode},
	{5,	AR5_StartOff1(3000) | AR5_MasterAluTrig},
	{6,	0},
	{7,	/* AR7_HSDiv(hsdiv) | AR7_RefClkDiv(refdiv) */ 0
		| AR7_ResAdj | AR7_NegPhase},
	{11,	0x7ff0000},
	{12,	0x0000000},
	{14,	0},
	/* finally, reset */
	{4,	AR4_EFlagHiZN | AR4_MasterReset | AR4_StartTimer(0)},
};

struct acam_mode_setup {
	enum fd_acam_modes mode;
	char *name;
	struct acam_init_data *data;
	int data_size;
};

static struct acam_mode_setup fd_acam_table[] = {
	{
		ACAM_RMODE, "R",
		acam_init_rmode, ARRAY_SIZE(acam_init_rmode),
	},
	{
		ACAM_IMODE, "I",
		acam_init_imode, ARRAY_SIZE(acam_init_imode)
	},
};

/* To configure the thing, follow the table, but treat 5 and 7 as special */
static int __acam_config(struct spec_fd *fd, struct acam_mode_setup *s)
{
	int i, hsdiv, refdiv, reg7val;
	struct acam_init_data *p;
	uint32_t regval;
	unsigned long j;

	fd->bin = acam_calc_pll(ACAM_FP_TREF, ACAM_FP_BIN, &hsdiv, &refdiv);
	reg7val = AR7_HSDiv(hsdiv) | AR7_RefClkDiv(refdiv);

	pr_debug("%s: config for %s-mode (bin 0x%x, hsdiv %i, refdiv %i)\n",
		 __func__, s->name, fd->bin, hsdiv, refdiv);

	/* Disable TDC inputs prior to configuring */
	fd_writel(fd, FD_TDCSR_STOP_DIS | FD_TDCSR_START_DIS, FD_REG_TDCSR);

	for (p = s->data, i = 0; i < s->data_size; p++, i++) {
		regval = p->val;
		if (p->addr == 7)
			regval |= reg7val;
		if (p->addr == 5 && s->mode == ACAM_RMODE)
			regval |= AR5_StartOff1(fd->calib.acam_start_offset);
		acam_writel(fd, regval, p->addr);
	}

	/* Wait for the oscillator to lock */
	j = jiffies + 2 * HZ;
	while (time_before(jiffies, j)) {
		if (acam_is_pll_locked(fd))
			break;
		msleep(10);
	}
	if (time_after_eq(jiffies, j)) {
		pr_err("%s: ACAM PLL does not lock\n", __func__);
		return -EIO;
	}
	/* after config, set the FIFO address for further reads */
	acam_set_address(fd, 8);
	return 0;
}

int fd_acam_config(struct spec_fd *fd, enum fd_acam_modes mode)
{
	struct acam_mode_setup *s;
	int i;

	for (s = fd_acam_table, i = 0; i < ARRAY_SIZE(fd_acam_table); s++, i++)
		if (mode == s->mode)
			return __acam_config(fd, s);
	pr_err("%s: invalid mode %i\n", __func__, mode);
	return -EINVAL;
}

int fd_acam_init(struct spec_fd *fd)
{
	int ret;
	fd->acam_addr = -1; /* First time must be activated */

	acam_set_bypass(fd, 1); /* Driven by host, not core */

	if ( (ret = acam_test_bus(fd)) )
		return ret;

	if ( (ret = fd_acam_config(fd, ACAM_IMODE)) )
		return ret;

	if ( (ret = fd_calibrate_outputs(fd)) )
		return ret;

	if ( (ret = fd_acam_config(fd, ACAM_RMODE)) )
		return ret;

	acam_set_bypass(fd, 0); /* Driven by core, not host */

	/* Clear and disable the timestamp readout buffer */
	fd_writel(fd, FD_TSBCR_PURGE | FD_TSBCR_RST_SEQ, FD_REG_TSBCR);

	/*
	 * Program the ACAM-specific TS registers w pre-defined calib values:
	 * - bin -> internal timebase scalefactor (ADSFR),
	 * - Start offset (must be consistent with value in ACAM reg 4)
	 * - timestamp merging control register (ATMCR)
	 */
	fd_writel(fd, fd->calib.adsfr_val, FD_REG_ADSFR);
	fd_writel(fd, 3 * fd->calib.acam_start_offset, FD_REG_ASOR);
	fd_writel(fd, fd->calib.atmcr_val, FD_REG_ATMCR);

	/* Prepare the timely recalibration */
	setup_timer(&fd->temp_timer, fd_update_calibration, (unsigned long)fd);
	if (fd_calib_period_s)
		mod_timer(&fd->temp_timer, jiffies + HZ * fd_calib_period_s);

	return 0;
}

void fd_acam_exit(struct spec_fd *fd)
{
	del_timer_sync(&fd->temp_timer);
}
