/*
 * Calibrate the output path.
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
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>
#include "fine-delay.h"
#include "hw/fd_main_regs.h"
#include "hw/acam_gpx.h"
#include "hw/fd_channel_regs.h"

/* TEMP! */
static void acam_set_bypass(struct spec_fd *fd, int on)
{
	/* FIXME: this zeroes all other GCR bits */
	fd_writel(fd, on ? FD_GCR_BYPASS : 0, FD_REG_GCR);
}


static int acam_test_delay_transfer_function(struct spec_fd *fd)
{
	/* FIXME */
	return 0;
}

/* Evaluates 2nd order polynomial. Coefs have 32 fractional bits. */
static int fd_eval_polynomial(struct spec_fd *fd)
{
	int64_t x = fd->temp;
	int64_t *coef = fd->calib.frr_poly;

	return (coef[0] * x * x + coef[1] * x + coef[2]) >> 32;
}

/*
 * Measures the the FPGA-generated TDC start and the output of one of
 * the fine delay chips (channel) at a pre-defined number of taps
 * (fine). Retuns the delay in picoseconds. The measurement is
 * repeated and averaged (n_avgs) times. Also, the standard deviation
 * of the result can be written to (sdev) if it's not NULL.
 */
struct delay_stats {
	uint32_t avg;
	uint32_t min;
	uint32_t max;
};

/* Note: channel is the "internal" one: 0..3 */
static uint32_t output_delay_ps(struct spec_fd *fd, int ch, int fine, int n,
				struct delay_stats *stats)
{
	int i;
	uint32_t res, *results;
	uint64_t acc = 0;

	results = kmalloc(n * sizeof(*results), GFP_KERNEL);
	if (!results)
		return -ENOMEM;

	/* Disable the output for the channel being calibrated */
	fd_gpio_clr(fd, FD_GPIO_OUTPUT_EN(FD_CH_EXT(ch)));

	/* Enable the stop input in ACAM for the channel being calibrated */
	acam_writel(fd, AR0_TRiseEn(0) | AR0_TRiseEn(FD_CH_EXT(ch))
		    | AR0_HQSel | AR0_ROsc, 0);

	/* Program the output delay line setpoint */
	fd_ch_writel(fd, ch, fine, FD_REG_FRR);
	fd_ch_writel(fd, ch, FD_DCR_ENABLE | FD_DCR_MODE | FD_DCR_UPDATE,
		    FD_REG_DCR);
	fd_ch_writel(fd, ch, FD_DCR_FORCE_DLY | FD_DCR_ENABLE, FD_REG_DCR);

	/*
	 * Set the calibration pulse mask to genrate calibration
	 * pulses only on one channel at a time.  This minimizes the
	 * crosstalk in the output buffer which can severely decrease
	 * the accuracy of calibration measurements
	 */
	fd_writel(fd, FD_CALR_PSEL_W(1 << ch), FD_REG_CALR);
	udelay(100);

	/* Do n_avgs single measurements and average */
	for (i = 0; i < n; i++) {
		uint32_t fr;
		/* Re-arm the ACAM (it's working in a single-shot mode) */
		fd_writel(fd, FD_TDCSR_ALUTRIG, FD_REG_TDCSR);
		udelay(100);
		/* Produce a calib pulse on the TDC start and the output ch */
		fd_writel(fd, FD_CALR_CAL_PULSE |
			  FD_CALR_PSEL_W(1 << ch), FD_REG_CALR);
		udelay(10000);
		/* read the tag, convert to picoseconds (fixed point: 16.16) */
		fr = acam_readl(fd, 8 /* fifo */);

		res = fr & 0x1ffff * fd->bin * 3; /* bin is 16.16 already */
		printk("%i: %08x, 0x%08x\n", fine, fr, res);
		results[i] = res;
		acc += res;
	}
	fd_ch_writel(fd, ch, 0, FD_REG_DCR);

	/* Calculate avg, min max */
	acc = (acc + n / 2) / n;
	if (stats) {
		stats->avg = acc;
		stats->min = ~0;
		stats->max = 0;
		for (i = 0; i < n; i++) {
			if (results[i] > stats->max) stats->max = results[i];
			if (results[i] < stats->min) stats->min = results[i];
		}
	}
	kfree(results);

	return acc;
}

static int fd_find_8ns_tap(struct spec_fd *fd, int ch)
{
	int l = 0, mid, r = FD_NUM_TAPS - 1;
	uint32_t bias, dly;

	/*
	 * Measure the delay at zero setting, so it can be further
	 * subtracted to get only the delay part introduced by the
	 * delay line (ingoring the TDC, FPGA and routing delays).
	 */
	bias = output_delay_ps(fd, ch, 0, FD_CAL_STEPS, NULL);
	while( r - l > 1) {
		mid = ( l + r) / 2;
		dly = output_delay_ps(fd, ch, mid, FD_CAL_STEPS, NULL)
			- bias;
		printk("%i %i %i %08xx (bias %08x)\n", l, r, mid, dly, bias);

		if(dly < 8000 << 16)
			l = mid;
		else
			r = mid;
	}
	return l;

}

int fd_calibrate_outputs(struct spec_fd *fd)
{
	int ret, ch;
	int measured, fitted;

	acam_set_bypass(fd, 1); /* not useful */
	fd_writel(fd, FD_TDCSR_START_EN | FD_TDCSR_STOP_EN, FD_REG_TDCSR);

	if ((ret = acam_test_delay_transfer_function(fd)) < 0)
		return ret;
	fitted = fd_eval_polynomial(fd);
	for (ch = FD_CH_1; ch <= FD_CH_LAST; ch++) {
		fd_read_temp(fd, 0);
		measured = fd_find_8ns_tap(fd, ch);
		fd->ch[ch].frr_cur = measured;
		fd->ch[ch].frr_offset = measured - fitted;

		pr_info("%s: ch %i: 8ns @ %i (f %i, offset %i, t %i.%02i)\n",
			__func__, FD_CH_EXT(ch),
			fd->ch[ch].frr_cur, fitted, fd->ch[ch].frr_offset,
			fd->temp / 16, (fd->temp & 0xf) * 100 / 16);
	}
	return 0;
}

