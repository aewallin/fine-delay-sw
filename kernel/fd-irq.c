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
#include "hw/vic_regs.h"

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

static void fd_ts_add(struct fd_time *t, int64_t pico)
{
	uint32_t coarse, frac;

	/* FIXME: we really need to pre-convert pico to internal repres. */
	if (pico < 0) {
		fd_ts_sub(t, -pico);
		return;
	}

	fd_split_pico(pico, &coarse, &frac);
	t->frac += frac;
	t->coarse += coarse;
	if (t->frac >= 4096) {
		t->frac -= 4096;
		t->coarse++;
	}
	if (t->coarse >= 125*1000*1000) {
		t->coarse -= 125*1000*1000;
		t->utc++;
	}
}

static inline void fd_normalize_time(struct fd_dev *fd, struct fd_time *t)
{
	/* The coarse count may be negative, because of how it works */
	if (t->coarse & (1<<27)) { // coarse is 28 bits
		/* we may get 0xfff.ffef..0xffff.ffff -- 125M == 0x773.5940 */
		t->coarse += 125000000;
		t->coarse &= 0xfffffff;
		t->utc--;
	} else if(t->coarse >= 125000000) {
		t->coarse -= 125000000;
		t->utc++;
	}

	fd_ts_add(t, fd->calib.tdc_zero_offset);
}


/* This is called from outside, too */
int fd_read_sw_fifo(struct fd_dev *fd, struct zio_channel *chan)
{
	struct zio_control *ctrl;
	struct zio_ti *ti = chan->cset->ti;
	uint32_t *v;
	int i, j;
	struct fd_time t, *tp;
	unsigned long flags;

	if (fd->sw_fifo.tail == fd->sw_fifo.head)
		return -EAGAIN;
	/*
	 * Proceed even if no active block is there. The buffer may be
	 * full, but we need to keep the trigger armed for next time,
	 * so deal with data and return success. If we -EAGAIN when
	 * !chan->active_block is null, we'll miss an irq to restar the loop.
	 */

	/* Copy the sample to a local variable, to release the lock soon */
	spin_lock_irqsave(&fd->lock, flags);
	i = fd->sw_fifo.tail % fd_sw_fifo_len;
	t = fd->sw_fifo.t[i];
	fd->sw_fifo.tail++;
	spin_unlock_irqrestore(&fd->lock, flags);

	fd_normalize_time(fd, &t);

	/* Write the timestamp in the trigger, it will reach the control */
	ti->tstamp.tv_sec = t.utc;
	ti->tstamp.tv_nsec = t.coarse * 8;
	ti->tstamp_extra = t.frac;

	/*
	 * This is different than it was. We used to fill the active block,
	 * but now zio copies chan->current_ctrl at a later time, so we
	 * must fill _those_ attributes instead
	 */
	/* The input data is written to attribute values in the active block. */
	ctrl = chan->current_ctrl;
	v = ctrl->attr_channel.ext_val;
	v[FD_ATTR_TDC_UTC_H]	= t.utc >> 32;
	v[FD_ATTR_TDC_UTC_L]	= t.utc;
	v[FD_ATTR_TDC_COARSE]	= t.coarse;
	v[FD_ATTR_TDC_FRAC]	= t.frac;
	v[FD_ATTR_TDC_SEQ]	= t.seq_id;
	v[FD_ATTR_TDC_CHAN]	= t.channel;
	v[FD_ATTR_TDC_FLAGS]	= fd->tdc_flags;
	v[FD_ATTR_TDC_OFFSET]	= fd->calib.tdc_zero_offset;
	v[FD_ATTR_TDC_USER_OFF]	= fd->tdc_user_offset;

	fd_apply_offset(v + FD_ATTR_TDC_UTC_H, fd->tdc_user_offset);

	/* We also need a copy within the device, so sysfs can read it */
	memcpy(fd->tdc_attrs, v + FD_ATTR_DEV__LAST, sizeof(fd->tdc_attrs));

	if (ctrl->ssize == 0) /* normal TDC device: no data */
		return 0;

	/*
	 * If we are returning raw data in the payload, cluster as many
	 * samples as they fit, or as many as the fifo has. If a block is there.
	 */
	if (!chan->active_block)
		return 0;

	tp = chan->active_block->data;
	*tp++ = t; /* already normalized, above */

	for (j = 1; j < ctrl->nsamples; j++, tp++) {
		spin_lock_irqsave(&fd->lock, flags);
		if (fd->sw_fifo.tail == fd->sw_fifo.head) {
			spin_unlock_irqrestore(&fd->lock, flags);
			break;
		}
		i = fd->sw_fifo.tail % fd_sw_fifo_len;
		*tp = fd->sw_fifo.t[i];
		fd->sw_fifo.tail++;
		spin_unlock_irqrestore(&fd->lock, flags);
		fd_normalize_time(fd, tp);
	}
	ctrl->nsamples = j;
	chan->active_block->datalen = j * ctrl->ssize;
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

	spin_lock_irqsave(&fd->lock, flags);
	t = fd->sw_fifo.t;
	t += fd->sw_fifo.head % fd_sw_fifo_len;

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
	diff = fd->sw_fifo.head - fd->sw_fifo.tail;
	fd->sw_fifo.head++;
	if (diff >= fd_sw_fifo_len)
		fd->sw_fifo.tail += fd_sw_fifo_len / 2;
	spin_unlock_irqrestore(&fd->lock, flags);

	BUG_ON(diff < 0);
	if (diff >= fd_sw_fifo_len)
		dev_warn(fd->fmc->hwdev, "Fifo overflow: "
			 " dropped %i samples (%li -> %li == %li)\n",
			 fd_sw_fifo_len / 2,
			 fd->sw_fifo.tail, fd->sw_fifo.head, diff);

	return 0;
}

/*
 * We have a timer, used to poll for input samples, until the interrupt
 * is there. A timer duration of 0 selects the interrupt.
 */
static int fd_timer_period_ms = 0;
module_param_named(timer_ms, fd_timer_period_ms, int, 0444);

static int fd_timer_period_jiffies; /* converted from ms at init time */

/* This acts as either a timer or an interrupt tasklet */
static void fd_tlet(unsigned long arg)
{
	struct fd_dev *fd = (void *)arg;
	struct zio_device *zdev = fd->zdev;
	struct zio_channel *chan = zdev->cset[0].chan;

	/* If we have no interrupt, read the hw fifo now */
	if (fd_timer_period_ms) {
		while (!fd_read_hw_fifo(fd))
			;
		mod_timer(&fd->fifo_timer, jiffies + fd_timer_period_jiffies);
	}

	/* FIXME: race condition */
	if (!test_bit(FD_FLAG_INPUT_READY, &fd->flags))
		return;

	/* there is an active block, try reading an accumulated sample */
	if (fd_read_sw_fifo(fd, chan) == 0) {
		clear_bit(FD_FLAG_INPUT_READY, &fd->flags);
		zio_trigger_data_done(chan->cset);
	}
}

irqreturn_t fd_irq_handler(int irq, void *dev_id)
{
	struct fmc_device *fmc = dev_id;
	struct fd_dev *fd = fmc->mezzanine_data;

	if ((fd_readl(fd, FD_REG_TSBCR) & FD_TSBCR_EMPTY))
		goto out_unexpected; /* bah! */

	/*
	 * We must empty the fifo in hardware, and ack at this point.
	 * I used to disable_irq() and empty the fifo in the tasklet,
	 * but it doesn't work because the hw request is still pending
	 */
	while (!fd_read_hw_fifo(fd))
		;
	tasklet_schedule(&fd->tlet);

out_unexpected:
	/*
	 * This may be an unexpected interrupt (it may not even be
	 * use, but still on the SPEC we must ack, or the system locks
	 * up, entering the interrupt again and again
	 */
	fmc->op->irq_ack(fmc);
	fmc_writel(fmc, 0, fd->fd_vic_base + VIC_REG_EOIR);
	return IRQ_HANDLED;
}

/* Unfortunately, on the spec this is GPIO9, i.e. IRQ(1) */
static struct fmc_gpio fd_gpio_on[] = {
	{
		.gpio = FMC_GPIO_IRQ(1),
		.mode = GPIOF_DIR_IN,
		.irqmode = IRQF_TRIGGER_RISING,
	}
};

static struct fmc_gpio fd_gpio_off[] = {
	{
		.gpio = FMC_GPIO_IRQ(1),
		.mode = GPIOF_DIR_IN,
		.irqmode = 0,
	}
};


int fd_irq_init(struct fd_dev *fd)
{
	struct fmc_device *fmc = fd->fmc;
	uint32_t vic_ctl;

	/* Check that the sw fifo size is a power of two */
	if (fd_sw_fifo_len & (fd_sw_fifo_len - 1)) {
		dev_err(&fd->fmc->dev,
			"fifo len must be a power of 2 (not %d = 0x%x)\n",
		        fd_sw_fifo_len, fd_sw_fifo_len);
		return -EINVAL;
	}

	fd->sw_fifo.t = kmalloc(fd_sw_fifo_len * sizeof(*fd->sw_fifo.t),
				GFP_KERNEL);
	if (!fd->sw_fifo.t)
		return -ENOMEM;

	fd_timer_period_jiffies = msecs_to_jiffies(fd_timer_period_ms);
	/*
	 * According to the period, this can work with a timer (old way)
	 * or a custom tasklet (newer). Init both anyways, no harm is done.
	 */
	setup_timer(&fd->fifo_timer, fd_tlet, (unsigned long)fd);
	tasklet_init(&fd->tlet, fd_tlet, (unsigned long)fd);

	if (fd_timer_period_ms) {
		dev_info(&fd->fmc->dev,"Using a timer for input (%i ms)\n",
			 jiffies_to_msecs(fd_timer_period_jiffies));
		mod_timer(&fd->fifo_timer, jiffies + fd_timer_period_jiffies);
	} else {
		dev_info(fd->fmc->hwdev, "Using interrupts for input\n");
		fmc->op->irq_request(fmc, fd_irq_handler, "fine-delay",
				     IRQF_SHARED);

		/*
		 * Then, configure the hardware: first fine delay,
		 * then vic, and finally the carrier
		 */

		/* current VHDL has a buglet: timeout is 8ns, not 1ms each */
		fd_writel(fd, FD_TSBIR_TIMEOUT_W(768) /* should be ms */
			  | FD_TSBIR_THRESHOLD_W(15), /* samples */
			  FD_REG_TSBIR);
		fd_writel(fd, FD_EIC_IER_TS_BUF_NOTEMPTY, FD_REG_EIC_IER);

		/* 4us edge emulation timer (counts in 16ns steps) */
		vic_ctl = VIC_CTL_EMU_EDGE | VIC_CTL_EMU_LEN_W(4000 / 16);
		fmc_writel(fmc, vic_ctl | VIC_CTL_ENABLE | VIC_CTL_POL,
			   fd->fd_vic_base + VIC_REG_CTL);
		fmc_writel(fmc, 1, fd->fd_vic_base + VIC_REG_IER);

		fmc->op->gpio_config(fmc, fd_gpio_on, ARRAY_SIZE(fd_gpio_on));
	}

	/* let it run... */
	fd_writel(fd, FD_GCR_INPUT_EN, FD_REG_GCR);

	return 0;
}

void fd_irq_exit(struct fd_dev *fd)
{
	struct fmc_device *fmc = fd->fmc;

	if (fd_timer_period_ms) {
		del_timer_sync(&fd->fifo_timer);
	} else {
		/* disable interrupts: first carrier, than vic, then fd */
		fmc->op->gpio_config(fmc, fd_gpio_off, ARRAY_SIZE(fd_gpio_off));
		fmc_writel(fmc, 1, fd->fd_vic_base + VIC_REG_IDR);
		fd_writel(fd, ~0, FD_REG_EIC_IDR);
		fmc_writel(fmc, VIC_CTL_POL, fd->fd_vic_base + VIC_REG_CTL);
		fmc->op->irq_free(fmc);
	}
	kfree(fd->sw_fifo.t);
}
