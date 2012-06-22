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
//#include <linux/math64.h>
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
	uint64_t avg;
	uint64_t min;
	uint64_t max;
};

/* Note: channel is the "internal" one: 0..3 */
static uint64_t output_delay_ps(struct spec_fd *fd, int ch, int fine, int n,
				struct delay_stats *stats)
{
	int i;
	uint64_t *results;
	uint64_t res, acc = 0;

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
	udelay(1);

	/* Do n_avgs single measurements and average */
	for (i = 0; i < n; i++) {
		uint32_t fr;
		/* Re-arm the ACAM (it's working in a single-shot mode) */
		fd_writel(fd, FD_TDCSR_ALUTRIG, FD_REG_TDCSR);
		udelay(1);
		/* Produce a calib pulse on the TDC start and the output ch */
		fd_writel(fd, FD_CALR_CAL_PULSE |
			  FD_CALR_PSEL_W(1 << ch), FD_REG_CALR);
		udelay(1);
		/* read the tag, convert to picoseconds (fixed point: 16.16) */
		fr = acam_readl(fd, 8 /* fifo */) & 0x1ffff;

		/*
		 * This is I-Mode, but fd->bin is for R-Mode, so 3x
		 * Then, fr is around 0xc00, bin is 0x50.0000: use 3LL for 64b
		 */
		res = fr * 3LL * fd->bin;
		if (fd->verbose > 3)
			pr_info("%s: ch %i, fine %i, bin %x got %08x, "
				"res 0x%016llx\n", __func__, ch, fine,
				fd->bin, fr, res);
		results[i] = res;
		acc += res;
	}
	fd_ch_writel(fd, ch, 0, FD_REG_DCR);

	/* Calculate avg, min max */
	acc = (acc + n / 2) / n;
	if (stats) {
		stats->avg = acc;
		stats->min = ~0LL;
		stats->max = 0LL;
		for (i = 0; i < n; i++) {
			if (results[i] > stats->max) stats->max = results[i];
			if (results[i] < stats->min) stats->min = results[i];
		}
		if (fd->verbose > 2)
			pr_info("%s: ch %i, taps %i, count %i, result %llx "
				"(max-min %llx)\n", __func__, ch, fine, n,
				stats->avg, stats->max - stats->min);
	}
	kfree(results);

	return acc;
}

static void __pr_fixed(char *head, uint64_t val, char *tail)
{
	printk("%s%i.%03i%s", head, (int)(val >> 16),
	       ((int)(val & 0xffff) * 1000) >> 16, tail);
}

static int fd_find_8ns_tap(struct spec_fd *fd, int ch)
{
	int l = 0, mid, r = FD_NUM_TAPS - 1;
	uint64_t bias, dly;
	struct delay_stats stats;

	/*
	 * Measure the delay at zero setting, so it can be further
	 * subtracted to get only the delay part introduced by the
	 * delay line (ingoring the TDC, FPGA and routing delays).
	 */
	bias = output_delay_ps(fd, ch, 0, FD_CAL_STEPS, NULL);
	while( r - l > 1) {
		mid = ( l + r) / 2;
		dly = output_delay_ps(fd, ch, mid, FD_CAL_STEPS, &stats) - bias;
		if (fd->verbose > 1) {
			printk("%s: ch%i @ %-5i: ", __func__, ch, mid);
			__pr_fixed("bias ", bias, ", ");
			__pr_fixed("min ", stats.min - bias, ", ");
			__pr_fixed("avg ", stats.avg - bias, ", ");
			__pr_fixed("max ", stats.max - bias, "\n");
		}

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
	int measured, fitted, new;

	acam_set_bypass(fd, 1); /* not useful */
	fd_writel(fd, FD_TDCSR_START_EN | FD_TDCSR_STOP_EN, FD_REG_TDCSR);

	if ((ret = acam_test_delay_transfer_function(fd)) < 0)
		return ret;

	fd_read_temp(fd, 0);
	fitted = fd_eval_polynomial(fd);

	for (ch = FD_CH_1; ch <= FD_CH_LAST; ch++) {
		measured = fd_find_8ns_tap(fd, ch);
		new = measured;
		fd->ch[ch].frr_offset = new - fitted;

		fd_ch_writel(fd, ch, new, FD_REG_FRR);
		fd->ch[ch].frr_cur = new;
		if (1) {
			pr_info("%s: ch%i: 8ns @%i (f %i, off %i, t %i.%02i)\n",
				__func__, FD_CH_EXT(ch),
				new, fitted, fd->ch[ch].frr_offset,
				fd->temp / 16, (fd->temp & 0xf) * 100 / 16);
		}
	}
	return 0;
}

/* Called from a timer any few seconds */
void fd_update_calibration(unsigned long arg)
{
	struct spec_fd *fd = (void *)arg;
	int ch, fitted, new;

	fd_read_temp(fd, 0 /* not verbose */);
	fitted = fd_eval_polynomial(fd);

	for (ch = FD_CH_1; ch <= FD_CH_LAST; ch++) {
		new = fitted + fd->ch[ch].frr_offset;
		fd_ch_writel(fd, ch, new, FD_REG_FRR);
		fd->ch[ch].frr_cur = new;
		if (0) {
			pr_info("%s: ch%i: 8ns @%i (f %i, off %i, t %i.%02i)\n",
				__func__, FD_CH_EXT(ch),
				new, fitted, fd->ch[ch].frr_offset,
				fd->temp / 16, (fd->temp & 0xf) * 100 / 16);
		}
	}

	mod_timer(&fd->temp_timer, jiffies + HZ * fd_calib_period_s);
}

