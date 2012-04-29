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
#include <linux/io.h>

#include <linux/zio.h>
#include <linux/zio-buffer.h>
#include <linux/zio-trigger.h>

#include "spec.h"
#include "fine-delay.h"
#include "hw/fd_main_regs.h"

/*
 * We have a number of attributes here. For input channels they are:
 *
 * UTC-h (expected to be 0 untile 2038 a.d.),
 * UTC-l
 * coarse time
 * fractional time
 * sequential ID
 * channel
 *
 * See the enum in "fine-delay.h"
 */

static struct zio_attribute fd_zattr_input[] = {
	ZATTR_EXT_REG("utc-h", S_IRUGO,		FD_ATTR_IN_UTC_H, 0),
	ZATTR_EXT_REG("utc-l", S_IRUGO,		FD_ATTR_IN_UTC_L, 0),
	ZATTR_EXT_REG("coarse", S_IRUGO,	FD_ATTR_IN_COARSE, 0),
	ZATTR_EXT_REG("frac", S_IRUGO,		FD_ATTR_IN_FRAC, 0),
	ZATTR_EXT_REG("seq", S_IRUGO,		FD_ATTR_IN_SEQ, 0),
	ZATTR_EXT_REG("chan", S_IRUGO,		FD_ATTR_IN_CHAN, 0),
};

/* The sample size. Mandatory, device-wide */
DEFINE_ZATTR_STD(ZDEV, fd_zattr_dev) = {
	ZATTR_REG(zdev, ZATTR_NBITS, S_IRUGO, 0, 32), /* 32 bits. Really? */
};


static int fd_read_fifo(struct spec_fd *fd, struct zio_channel *chan)
{
	struct zio_control *ctrl;
	uint32_t *v, reg;

	if ((fd_readl(fd, FD_REG_TSBCR) & FD_TSBCR_EMPTY))
		return -EAGAIN;
	if (!chan->active_block)
		return 0;
	ctrl = zio_get_ctrl(chan->active_block);
	/* The input data is written to attributes */
	v = ctrl->attr_channel.ext_val;
	v[FD_ATTR_IN_UTC_H] = fd_readl(fd, FD_REG_TSBR_SECH) & 0xff;
	v[FD_ATTR_IN_UTC_L] = fd_readl(fd, FD_REG_TSBR_SECL);
	v[FD_ATTR_IN_COARSE] = fd_readl(fd, FD_REG_TSBR_CYCLES) & 0xfffffff;
	reg = fd_readl(fd, FD_REG_TSBR_FID);
	v[FD_ATTR_IN_FRAC] = FD_TSBR_FID_FINE_R(reg);
	v[FD_ATTR_IN_SEQ] = FD_TSBR_FID_SEQID_R(reg);
	v[FD_ATTR_IN_CHAN] = FD_TSBR_FID_CHANNEL_R(reg);
	return 0;
}

/*
 * We have a timer, used to poll for input samples, until the interrupt
 * is there. A timer duration of 0 selects the interrupt.
 */
static int fd_timer_period_ms = 100;
module_param_named(timer_ms, fd_timer_period_ms, int, 0444);

static int fd_timer_period_jiffies; /* converted from ms at init time */

static struct zio_device *__HACK__ZDEV__; /* don't do it! */
static struct spec_fd *__HACK__FD__; /* don't do it! */

static void fd_timer_fn(unsigned long arg)
{
	struct spec_fd *fd = (void *)arg;
	struct zio_channel *chan = NULL;
	struct zio_device *zdev = __HACK__ZDEV__;

	if (zdev) {
		chan = zdev->cset[0].chan;
	} else {
		/* nobody read the device so far: we lack the information */
		goto out;
	}

	/* FIXME: manage an array of input samples */
	if (fd_read_fifo(fd, chan) == 0) {
		if (chan->active_block)
			chan->cset->trig->t_op->data_done(chan->cset);
		else
			pr_err("data and no block\n");
	}
out:
	mod_timer(&fd->timer, jiffies + fd_timer_period_jiffies);
}

static int fd_output(struct zio_cset *cset)
{
	/* FIXME: the output channels */

	return 0; /* Already done, as the trigger is hardware */
}

/*
 * The input method will return immediately, because input is
 * asynchronous. The data_done callback is invoked when the block is
 * full.
 */
static int fd_input(struct zio_cset *cset)
{
	struct spec_fd *fd;

	__HACK__ZDEV__ = cset->zdev;
	fd = __HACK__FD__;

	/* Configure the device for input */
	if (!fd->flags & FD_FLAG_INPUT) {
		fd_writel(fd, FD_TSBCR_PURGE | FD_TSBCR_RST_SEQ, FD_REG_TSBCR);
		fd_writel(fd, FD_TSBCR_CHAN_MASK_W(1) | FD_TSBCR_ENABLE,
			  FD_REG_TSBCR);
		fd->flags |= FD_FLAG_INPUT;
	}

	return -EAGAIN; /* Will be completed over time */
}

/* We have 5 csets, since each output triggers separately */
static struct zio_cset fd_cset[] = {
	{
		SET_OBJECT_NAME("fd-input"),
		.raw_io =	fd_input,
		.n_chan =	1,
		.ssize =	4, /* FIXME: 0? */
		.flags =	ZIO_DIR_INPUT | ZCSET_TYPE_TIME,
		.zattr_set = {
			.ext_zattr = fd_zattr_input,
			.n_ext_attr = ARRAY_SIZE(fd_zattr_input),
		},
	},
	{
		SET_OBJECT_NAME("fd-ch1"),
		.raw_io =	fd_output,
		.n_chan =	1,
		.ssize =	4, /* FIXME: 0? */
		.flags =	ZIO_DIR_OUTPUT | ZCSET_TYPE_TIME,
	},
	{
		SET_OBJECT_NAME("fd-ch2"),
		.raw_io =	fd_output,
		.n_chan =	1,
		.ssize =	4, /* FIXME: 0? */
		.flags =	ZIO_DIR_OUTPUT | ZCSET_TYPE_TIME,
	},
	{
		SET_OBJECT_NAME("fd-ch3"),
		.raw_io =	fd_output,
		.n_chan =	1,
		.ssize =	4, /* FIXME: 0? */
		.flags =	ZIO_DIR_OUTPUT | ZCSET_TYPE_TIME,
	},
	{
		SET_OBJECT_NAME("fd-ch4"),
		.raw_io =	fd_output,
		.n_chan =	1,
		.ssize =	4, /* FIXME: 0? */
		.flags =	ZIO_DIR_OUTPUT | ZCSET_TYPE_TIME,
	},
};

static struct zio_device fd_tmpl = {
	.owner =		THIS_MODULE,
	.preferred_trigger =	"user",
	.cset =			fd_cset,
	.n_cset =		ARRAY_SIZE(fd_cset),
	.zattr_set = {
		.std_zattr= fd_zattr_dev,
	},
};

static const struct zio_device_id fd_table[] = {
	{"fd", &fd_tmpl},
	{},
};

static struct zio_driver fd_zdrv = {
	.driver = {
		.name = "fd",
		.owner = THIS_MODULE,
	},
	.id_table = fd_table,
};


/* Register and unregister are used to set up the template driver */
int fd_zio_register(void)
{
	int err;

	err = zio_register_driver(&fd_zdrv);
	if (err)
		return err;

	fd_timer_period_jiffies = msecs_to_jiffies(fd_timer_period_ms);
	if (fd_timer_period_ms) {
		pr_info("%s: using a timer for input stamps (%i ms)\n",
			KBUILD_MODNAME, fd_timer_period_ms);
	} else {
		pr_info("%s: NOT using interrupt (not implemented)\n",
			KBUILD_MODNAME);
		return -EINVAL;
	}

	return 0;
}

void fd_zio_unregister(void)
{
	zio_unregister_driver(&fd_zdrv);
	/* FIXME */
}

/* Init and exit are called for each FD card we have */
int fd_zio_init(struct spec_fd *fd)
{
	int err = 0;
	struct pci_dev *pdev;
	int dev_id;

	fd->zdev = zio_allocate_device();
	if (IS_ERR(fd->zdev))
		return PTR_ERR(fd->zdev);

	/* Our dev_id is bus+devfn */
	pdev = fd->spec->pdev;
	dev_id = (pdev->bus->number << 8) | pdev->devfn;
	fd->zdev->owner = THIS_MODULE;
	err = zio_register_device(fd->zdev, "fd", dev_id);
	if (err) {
		zio_free_device(fd->zdev);
		return err;
	}

	__HACK__FD__ = fd;

	setup_timer(&fd->timer, fd_timer_fn, (unsigned long)fd);
	if (fd_timer_period_ms)
		mod_timer(&fd->timer, jiffies + fd_timer_period_jiffies);
	return 0;
}

void fd_zio_exit(struct spec_fd *fd)
{
	del_timer_sync(&fd->timer);
	zio_unregister_device(fd->zdev);
	zio_free_device(fd->zdev);
}
