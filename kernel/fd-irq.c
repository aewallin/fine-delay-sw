/*
 * ZIO interface for the fine-delay driver
 *
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2 as published by the Free Software Foundation or, at your
 * option, any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>
#include <linux/io.h>

#include <linux/zio.h>
#include <linux/zio-buffer.h>
#include <linux/zio-trigger.h>

#include <linux/fmc.h>

#include "fine-delay.h"
#include "hw/fd_main_regs.h"
#include "hw/fd_channel_regs.h"

static int fd_sw_fifo_len = FD_SW_FIFO_LEN;
module_param_named(fifo_len, fd_sw_fifo_len, int, 0444);

/* Subtract an offset (used for the input timestamp) */
static void fd_ts_sub(struct fd_time *t, uint64_t pico)
{
	uint32_t coarse, frac;

	/* FIXME: we really need to pre-convert pico to internal repres. */
	fd_split_pico(pico, &coarse, &frac);
	if (t->frac >= frac) {
		t->frac -= frac;
	} else {
		t->frac = 4096 + t->frac - frac;
		coarse++;
	}
	if (t->coarse >= coarse) {
		t->coarse -= coarse;
	} else {
		t->coarse = 125*1000*1000 + t->coarse - coarse;
		t->utc--;
	}
}

/* This is called from outside, too */
int fd_read_sw_fifo(struct fd_dev *fd, struct zio_channel *chan)
{
	struct zio_control *ctrl;
	uint32_t *v;
	int i;
	struct fd_time t;
	unsigned long flags;

	if (!chan->active_block)
		return -EAGAIN;
	if (fd->sw_fifo.tail == fd->sw_fifo.head)
		return -EAGAIN;

	/* Copy the sample to local storage */
	spin_lock_irqsave(&fd->lock, flags);
	i = fd->sw_fifo.tail & (fd_sw_fifo_len - 1);
	t = fd->sw_fifo.t[i];
	fd->sw_fifo.tail++;
	spin_unlock_irqrestore(&fd->lock, flags);

	/* The coarse count may be negative, because of how it works */
	if (t.coarse & (1<<27)) { // coarse is 28 bits
		/* we may get 0xfff.ffef..0xffff.ffff -- 125M == 0x773.5940 */
		t.coarse += 125000000;
		t.coarse &= 0xfffffff;
		t.utc--;
	} else if(t.coarse > 125000000) {
		t.coarse -= 125000000;
		t.utc++;
	}

	fd_ts_sub(&t, fd->calib.tdc_zero_offset);

	/* The input data is written to attribute values in the active block. */
	ctrl = zio_get_ctrl(chan->active_block);
	v = ctrl->attr_channel.ext_val;
	v[FD_ATTR_TDC_UTC_H]	= t.utc >> 32;
	v[FD_ATTR_TDC_UTC_L]	= t.utc;
	v[FD_ATTR_TDC_COARSE]	= t.coarse;
	v[FD_ATTR_TDC_FRAC]	= t.frac;
	v[FD_ATTR_TDC_SEQ]	= t.seq_id;
	v[FD_ATTR_TDC_CHAN]	= t.channel;
	v[FD_ATTR_TDC_FLAGS]	= fd->calib.tdc_flags;
	v[FD_ATTR_TDC_OFFSET]	= fd->calib.tdc_zero_offset;
	v[FD_ATTR_TDC_USER_OFF]	= fd->calib.tdc_user_offset;

	fd_apply_offset(v + FD_ATTR_TDC_UTC_H, fd->calib.tdc_user_offset);

	/* We also need a copy within the device, so sysfs can read it */
	memcpy(fd->tdc_attrs, v + FD_ATTR_DEV__LAST, sizeof(fd->tdc_attrs));

	return 0;
}

/* This is local: reads the hw fifo and stores to the sw fifo */
static int fd_read_hw_fifo(struct fd_dev *fd)
{
	uint32_t reg;
	struct fd_time *t;
	unsigned long flags;
	signed long diff;

	if ((fd_readl(fd, FD_REG_TSBCR) & FD_TSBCR_EMPTY))
		return -EAGAIN;

	t = fd->sw_fifo.t;
	t += fd->sw_fifo.head & (fd_sw_fifo_len - 1);

	/* Fetch the fifo entry to registers, so we can read them */
	fd_writel(fd, FD_TSBR_ADVANCE_ADV, FD_REG_TSBR_ADVANCE);

	/* Read input data into the sofware fifo */
	t->utc = fd_readl(fd, FD_REG_TSBR_SECH) & 0xff;
	t->utc <<= 32;
	t->utc |= fd_readl(fd, FD_REG_TSBR_SECL);
	t->coarse = fd_readl(fd, FD_REG_TSBR_CYCLES) & 0xfffffff;
	reg = fd_readl(fd, FD_REG_TSBR_FID);
	t->frac = FD_TSBR_FID_FINE_R(reg);
	t->channel = FD_TSBR_FID_CHANNEL_R(reg);
	t->seq_id = FD_TSBR_FID_SEQID_R(reg);

	/* Then, increment head and make some checks */
	spin_lock_irqsave(&fd->lock, flags);
	diff = fd->sw_fifo.head - fd->sw_fifo.tail;
	fd->sw_fifo.head++;
	if (diff >= fd_sw_fifo_len)
		fd->sw_fifo.tail += fd_sw_fifo_len / 2;
	spin_unlock_irqrestore(&fd->lock, flags);

	BUG_ON(diff < 0);
	if (diff >= fd_sw_fifo_len)
		dev_warn(fd->fmc->hwdev, "Fifo overlow, dropped %i samples\n",
			 fd_sw_fifo_len / 2);

	return 0;
}

/*
 * We have a timer, used to poll for input samples, until the interrupt
 * is there. A timer duration of 0 selects the interrupt.
 */
static int fd_timer_period_ms = 10;
module_param_named(timer_ms, fd_timer_period_ms, int, 0444);

static int fd_timer_period_jiffies; /* converted from ms at init time */

static void fd_timer_fn(unsigned long arg)
{
	struct fd_dev *fd = (void *)arg;
	struct zio_channel *chan = NULL;
	struct zio_device *zdev = fd->zdev;
	int i;

	/* Always read the hardware fifo until empty */
	while (!fd_read_hw_fifo(fd))
		;

	if (zdev) {
		chan = zdev->cset[0].chan;
	} else {
		/* nobody read the device so far: we lack the information */
		goto out;
	}

	if (!test_bit(FD_FLAG_INPUT_READY, &fd->flags))
		goto out;

	/* there is an active block, try reading an accumulated sample */
	if (fd_read_sw_fifo(fd, chan) == 0) {
		clear_bit(FD_FLAG_INPUT_READY, &fd->flags);
		chan->cset->trig->t_op->data_done(chan->cset);
	}

out:
	/* Check all output channels with a pending block (FIXME: bad) */
	for (i = 1; i < 5; i++)
		if (test_and_clear_bit(FD_FLAG_DO_OUTPUT + i, &fd->flags)) {
			struct zio_cset *cset = fd->zdev->cset + i;
			cset->ti->t_op->data_done(cset);
			pr_debug("called data_done\n");
		}

	mod_timer(&fd->fifo_timer, jiffies + fd_timer_period_jiffies);
}


int fd_irq_init(struct fd_dev *fd)
{

	/* This is not per-device, but it works anyways */
	fd_timer_period_jiffies = msecs_to_jiffies(fd_timer_period_ms);
	if (fd_timer_period_ms) {
		pr_info("%s: using a timer for input stamps (%i ms)\n",
			KBUILD_MODNAME, fd_timer_period_ms);
	} else {
		pr_info("%s: NOT using interrupt (not implemented)\n",
			KBUILD_MODNAME);
		return -EINVAL;
	}

	/* Also, check that the sw fifo size is a power of two */
	if (fd_sw_fifo_len & (fd_sw_fifo_len - 1)) {
		pr_err("%s: fifo len must be a power of 2 (not %d = 0x%x)\n",
		       KBUILD_MODNAME, fd_sw_fifo_len, fd_sw_fifo_len);
		return -EINVAL;
	}

	fd->sw_fifo.t = kmalloc(fd_sw_fifo_len * sizeof(*fd->sw_fifo.t),
				GFP_KERNEL);
	if (!fd->sw_fifo.t)
		return -ENOMEM;

	setup_timer(&fd->fifo_timer, fd_timer_fn, (unsigned long)fd);
	if (fd_timer_period_ms)
		mod_timer(&fd->fifo_timer, jiffies + fd_timer_period_jiffies);

	fd_writel(fd, FD_GCR_INPUT_EN, FD_REG_GCR);
	return 0;
}

void fd_irq_exit(struct fd_dev *fd)
{
	del_timer_sync(&fd->fifo_timer);
	kfree(fd->sw_fifo.t);
}
