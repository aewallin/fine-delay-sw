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
#include <linux/io.h>

#include <linux/zio.h>
#include <linux/zio-buffer.h>
#include <linux/zio-trigger.h>

#include "spec.h"
#include "fine-delay.h"
#include "hw/fd_main_regs.h"
#include "hw/fd_channel_regs.h"

/* The sample size. Mandatory, device-wide */
DEFINE_ZATTR_STD(ZDEV, fd_zattr_dev_std) = {
	ZATTR_REG(zdev, ZATTR_NBITS, S_IRUGO, 0, 32), /* 32 bits. Really? */
};

/* Extended attributes for the device */
static struct zio_attribute fd_zattr_dev[] = {
	ZATTR_EXT_REG("version", S_IRUGO,		FD_ATTR_DEV_VERSION,
		      FDELAY_VERSION),
	ZATTR_EXT_REG("utc-h", S_IRUGO | S_IWUGO,	FD_ATTR_DEV_UTC_H, 0),
	ZATTR_EXT_REG("utc-l", S_IRUGO | S_IWUGO,	FD_ATTR_DEV_UTC_L, 0),
	ZATTR_EXT_REG("coarse", S_IRUGO | S_IWUGO,	FD_ATTR_DEV_COARSE, 0),
	ZATTR_EXT_REG("command", S_IWUGO,		FD_ATTR_DEV_COMMAND, 0),
};

/* Extended attributes for the TDC (== input) cset */
static struct zio_attribute fd_zattr_input[] = {
	ZATTR_EXT_REG("utc-h", S_IRUGO,		FD_ATTR_TDC_UTC_H, 0),
	ZATTR_EXT_REG("utc-l", S_IRUGO,		FD_ATTR_TDC_UTC_L, 0),
	ZATTR_EXT_REG("coarse", S_IRUGO,	FD_ATTR_TDC_COARSE, 0),
	ZATTR_EXT_REG("frac", S_IRUGO,		FD_ATTR_TDC_FRAC, 0),
	ZATTR_EXT_REG("seq", S_IRUGO,		FD_ATTR_TDC_SEQ, 0),
	ZATTR_EXT_REG("chan", S_IRUGO,		FD_ATTR_TDC_CHAN, 0),
	ZATTR_EXT_REG("flags", S_IRUGO|S_IWUGO,	FD_ATTR_TDC_FLAGS, 0),
	ZATTR_EXT_REG("offset", S_IRUGO,	FD_ATTR_TDC_OFFSET, 0),
};

/* Extended attributes for the output csets */
#define _RW_ (S_IRUGO | S_IWUGO)
static struct zio_attribute fd_zattr_output[] = {
	ZATTR_EXT_REG("mode", _RW_,		FD_ATTR_OUT_MODE, 0),
	ZATTR_EXT_REG("rep", _RW_,		FD_ATTR_OUT_REP, 0),
	ZATTR_EXT_REG("start-h", _RW_,		FD_ATTR_OUT_START_H, 0),
	ZATTR_EXT_REG("start-l", _RW_,		FD_ATTR_OUT_START_L, 0),
	ZATTR_EXT_REG("start-coarse", _RW_,	FD_ATTR_OUT_START_COARSE, 0),
	ZATTR_EXT_REG("start-fine", _RW_,	FD_ATTR_OUT_START_FINE, 0),
	ZATTR_EXT_REG("end-h", _RW_,		FD_ATTR_OUT_END_H, 0),
	ZATTR_EXT_REG("end-l", _RW_,		FD_ATTR_OUT_END_L, 0),
	ZATTR_EXT_REG("end-coarse", _RW_,	FD_ATTR_OUT_END_COARSE, 0),
	ZATTR_EXT_REG("end-fine", _RW_,		FD_ATTR_OUT_END_FINE, 0),
	ZATTR_EXT_REG("delta-l", _RW_,		FD_ATTR_OUT_DELTA_L, 0),
	ZATTR_EXT_REG("delta-coarse", _RW_,	FD_ATTR_OUT_DELTA_COARSE, 0),
	ZATTR_EXT_REG("delta-fine", _RW_,	FD_ATTR_OUT_DELTA_FINE, 0),
};


/* This identifies if our "struct device" is device, input, output */
enum fd_devtype {
	FD_TYPE_WHOLEDEV,
	FD_TYPE_INPUT,
	FD_TYPE_OUTPUT,
};

static enum fd_devtype __fd_get_type(struct device *dev)
{
	struct zio_obj_head *head = to_zio_head(dev);
	struct zio_cset *cset;

	if (head->zobj_type == ZDEV)
		return FD_TYPE_WHOLEDEV;
	cset = to_zio_cset(dev);
	if (cset->index == 0)
		return FD_TYPE_INPUT;
	return FD_TYPE_OUTPUT;
}

/* TDC input attributes: only the offset is special */
static int fd_zio_info_tdc(struct device *dev, struct zio_attribute *zattr,
			     uint32_t *usr_val)
{
	struct zio_cset *cset;
	struct spec_fd *fd;

	cset = to_zio_cset(dev);
	fd = cset->zdev->private_data;

	/*
	 * For efficiency reasons at read_fifo() time, we store an
	 * array of integers instead of filling attributes, so here
	 * pick the values from our array.
	 */
	*usr_val = fd->tdc_attrs[FD_CSET_INDEX(zattr->priv.addr)];

	return 0;
}

/* Overall and device-wide attributes: only get_time is special */
static int fd_zio_info_get(struct device *dev, struct zio_attribute *zattr,
			   uint32_t *usr_val)
{
	struct fd_time t;
	struct zio_device *zdev;
	struct spec_fd *fd;
	struct zio_attribute *attr;

	if (__fd_get_type(dev) == FD_TYPE_INPUT)
		return fd_zio_info_tdc(dev, zattr, usr_val);
	if (__fd_get_type(dev) != FD_TYPE_WHOLEDEV)
		return 0;
	if (zattr->priv.addr != FD_ATTR_DEV_UTC_H)
		return 0;
	/* reading utc-h calls an atomic get-time */
	zdev = to_zio_dev(dev);
	attr = zdev->zattr_set.ext_zattr;
	fd = zdev->private_data;
	fd_time_get(fd, &t, NULL);
	attr[FD_ATTR_DEV_UTC_H].value = t.utc >> 32;
	attr[FD_ATTR_DEV_UTC_L].value = t.utc & 0xffffffff;
	attr[FD_ATTR_DEV_COARSE].value = t.coarse;
	return 0;
}

/* TDC input attributes: the flags */
static int fd_zio_conf_tdc(struct device *dev, struct zio_attribute *zattr,
			    uint32_t  usr_val)
{
	struct zio_cset *cset;
	struct spec_fd *fd;
	uint32_t reg;
	int change;

	cset = to_zio_cset(dev);
	fd = cset->zdev->private_data;

	if (zattr->priv.addr != FD_ATTR_TDC_FLAGS)
		goto out;

	change = zattr->value ^ usr_val; /* old xor new */

	/* No need to lock, as configuration is serialized by zio-core */
	if (change & FD_TDCF_DISABLE_INPUT) {
		reg = fd_readl(fd, FD_REG_GCR);
		if (usr_val & FD_TDCF_DISABLE_INPUT)
			reg &= ~FD_GCR_INPUT_EN;
		else
			reg |= FD_GCR_INPUT_EN;
		fd_writel(fd, reg, FD_REG_GCR);
	}

	if (change & FD_TDCF_DISABLE_TSTAMP) {
		reg = fd_readl(fd, FD_REG_TSBCR);
		if (usr_val & FD_TDCF_DISABLE_TSTAMP)
			reg &= ~FD_TSBCR_ENABLE;
		else
			reg |= FD_TSBCR_ENABLE;
		fd_writel(fd, reg, FD_REG_TSBCR);
	}

	if (change & FD_TDCF_TERM_50) {
		if (usr_val & FD_TDCF_TERM_50)
			fd_gpio_set(fd, FD_GPIO_TERM_EN);
		else
			fd_gpio_clr(fd, FD_GPIO_TERM_EN);
	}
out:
	/* We need to store in the other array too (see info_tdc() above) */
	fd->tdc_attrs[FD_CSET_INDEX(zattr->priv.addr)] = usr_val;

	return 0;
}

/* conf_set dispatcher and  and device-wide attributes */
static int fd_zio_conf_set(struct device *dev, struct zio_attribute *zattr,
			    uint32_t  usr_val)
{
	struct fd_time t;
	struct zio_device *zdev;
	struct spec_fd *fd;
	struct zio_attribute *attr;

	if (__fd_get_type(dev) != FD_TYPE_WHOLEDEV)
		return fd_zio_conf_tdc(dev, zattr, usr_val);

	zdev = to_zio_dev(dev);
	attr = zdev->zattr_set.ext_zattr;
	fd = zdev->private_data;

	if (zattr->priv.addr == FD_ATTR_DEV_UTC_H) {
		/* writing utc-h calls an atomic set-time */
		t.utc = (uint64_t)attr[FD_ATTR_DEV_UTC_H].value << 32;
		t.utc |= attr[FD_ATTR_DEV_UTC_L].value;
		t.coarse = attr[FD_ATTR_DEV_COARSE].value;
		fd_time_set(fd, &t, NULL);
		return 0;
	}

	/* Not command, nothing to do */
	if (zattr->priv.addr != FD_ATTR_DEV_COMMAND)
		return 0;

	switch(usr_val) {
	case FD_CMD_HOST_TIME:
		return fd_time_set(fd, NULL, NULL);
	default:
		return -EINVAL;
	}
}

/*
 * We are over with attributes, now there's real I/O
 */

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

static int fd_read_fifo(struct spec_fd *fd, struct zio_channel *chan)
{
	struct zio_control *ctrl;
	uint32_t *v, reg;
	struct fd_time t;

	if ((fd_readl(fd, FD_REG_TSBCR) & FD_TSBCR_EMPTY))
		return -EAGAIN;
	if (!chan->active_block)
		return 0;

	/* First, read input data into a local struct to fix the offset */
	t.utc = fd_readl(fd, FD_REG_TSBR_SECH) & 0xff;
	t.utc <<= 32;
	t.utc |= fd_readl(fd, FD_REG_TSBR_SECL);
	t.coarse = fd_readl(fd, FD_REG_TSBR_CYCLES) & 0xfffffff;
	reg = fd_readl(fd, FD_REG_TSBR_FID);
	t.frac = FD_TSBR_FID_FINE_R(reg);
	t.channel = FD_TSBR_FID_CHANNEL_R(reg);
	t.seq_id = FD_TSBR_FID_SEQID_R(reg);

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
	v[FD_ATTR_TDC_OFFSET]	= fd->calib.tdc_zero_offset;

	/* We also need a copy within the device, so sysfs can read it */
	memcpy(fd->tdc_attrs, v + FD_ATTR_DEV__LAST, sizeof(fd->tdc_attrs));

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
	struct spec_fd *fd = (void *)arg;
	struct zio_channel *chan = NULL;
	struct zio_device *zdev = fd->zdev;
	int i;

	if (zdev) {
		chan = zdev->cset[0].chan;
	} else {
		/* nobody read the device so far: we lack the information */
		goto out;
	}

	/* FIXME: manage an array of input samples */
	if (!test_bit(FD_FLAG_INPUT_READY, &fd->flags))
		goto out;

	/* there is an active block, try reading fifo */
	if (fd_read_fifo(fd, chan) == 0) {
		clear_bit(FD_FLAG_INPUT_READY, &fd->flags);
		chan->cset->trig->t_op->data_done(chan->cset);
	}

out:
	/* Check all output channels with a pending block (FIXME: bad) */
	for (i = 1; i < 5; i++)
		if (test_and_clear_bit(FD_FLAG_DO_OUTPUT + i, &fd->flags)) {
			struct zio_cset *cset = fd->zdev->cset + i;
			cset->ti->t_op->data_done(cset);
			printk("called data_done\n");
		}

	mod_timer(&fd->fifo_timer, jiffies + fd_timer_period_jiffies);
}

/* Internal output engine */
static void __fd_zio_output(struct spec_fd *fd, int index1_4, uint32_t *attrs)
{
	int ch = index1_4 - 1;
	int mode = attrs[FD_ATTR_OUT_MODE];
	int rep = attrs[FD_ATTR_OUT_REP];
	int dcr;

	if (mode == FD_OUT_MODE_DISABLED) {
		fd_gpio_clr(fd, FD_GPIO_OUTPUT_EN(index1_4));
		return;
	}

	if (mode == FD_OUT_MODE_DELAY) {
		/* FIXME: subtract zero offset */
	}
	fd_ch_writel(fd, ch, fd->ch[ch].frr_cur,  FD_REG_FRR);

	fd_ch_writel(fd, ch, attrs[FD_ATTR_OUT_START_H],      FD_REG_U_STARTH);
	fd_ch_writel(fd, ch, attrs[FD_ATTR_OUT_START_L],      FD_REG_U_STARTL);
	fd_ch_writel(fd, ch, attrs[FD_ATTR_OUT_START_COARSE], FD_REG_C_START);
	fd_ch_writel(fd, ch, attrs[FD_ATTR_OUT_START_FINE],   FD_REG_F_START);

	fd_ch_writel(fd, ch, attrs[FD_ATTR_OUT_END_H],      FD_REG_U_ENDH);
	fd_ch_writel(fd, ch, attrs[FD_ATTR_OUT_END_L],      FD_REG_U_ENDL);
	fd_ch_writel(fd, ch, attrs[FD_ATTR_OUT_END_COARSE], FD_REG_C_END);
	fd_ch_writel(fd, ch, attrs[FD_ATTR_OUT_END_FINE],   FD_REG_F_END);

	fd_ch_writel(fd, ch, attrs[FD_ATTR_OUT_DELTA_L],      FD_REG_U_DELTA);
	fd_ch_writel(fd, ch, attrs[FD_ATTR_OUT_DELTA_COARSE], FD_REG_C_DELTA);
	fd_ch_writel(fd, ch, attrs[FD_ATTR_OUT_DELTA_FINE],   FD_REG_F_DELTA);

	if (mode == FD_OUT_MODE_DELAY) {
		dcr = 0;
		fd_ch_writel(fd, ch, FD_RCR_REP_CNT_W(rep - 1)
			     | (rep < 0 ? FD_RCR_CONT : 0), FD_REG_RCR);
	} else {
		dcr = FD_DCR_MODE;
		fd_ch_writel(fd, ch, FD_RCR_REP_CNT_W(rep < 0 ? 0 : rep - 1)
			    | (rep < 0 ? FD_RCR_CONT : 0), FD_REG_RCR);
	}

	/*
	 * For narrowly spaced pulses, we don't have enough time to reload
	 * the tap number into the corresponding SY89295.
	 * Therefore, the width/spacing resolution is limited to 4 ns.
	 * We put the threshold at 200ns ==> coarse == 25
	 */

	/* FIXME: if((delta_ps - width_ps) < 200000 ||
	   (width_ps < 200000)) dcr = FD_DCR_NO_FINE; */
	//dcr |= FD_DCR_NO_FINE;

	fd_ch_writel(fd, ch, dcr | FD_DCR_UPDATE, FD_REG_DCR);
	fd_ch_writel(fd, ch, dcr | FD_DCR_ENABLE, FD_REG_DCR);
	if (mode == FD_OUT_MODE_PULSE)
		fd_ch_writel(fd, ch, dcr | FD_DCR_ENABLE | FD_DCR_PG_ARM,
			     FD_REG_DCR);

	fd_gpio_set(fd, FD_GPIO_OUTPUT_EN(index1_4));
}

/* This is called on user write */
static int fd_zio_output(struct zio_cset *cset)
{
	int i;
	struct spec_fd *fd;
	struct zio_control *ctrl;

	fd = cset->zdev->private_data;
	ctrl = zio_get_ctrl(cset->chan->active_block);

	for (i = 0; i < 4; i++)
		printk("triggered %i: %x (%i)\n", i,
		       fd_ch_readl(fd, i, FD_REG_DCR),
		       fd_ch_readl(fd, i, FD_REG_DCR) & FD_DCR_PG_TRIG ? 1: 0);

	pr_info("%s: attrs: ", __func__);
	for (i = FD_ATTR_DEV__LAST; i < FD_ATTR_OUT__LAST; i++)
		printk("%08x%c", ctrl->attr_channel.ext_val[i],
		       i == FD_ATTR_OUT__LAST -1 ? '\n' : ' ');

	__fd_zio_output(fd, cset->index, ctrl->attr_channel.ext_val);
	/*
	 * There's a buglet in this version of zio: we can't
	 * just return 0 to say "done". We need to do it later.
	 */
	set_bit(FD_FLAG_DO_OUTPUT + cset->index, &fd->flags);
	return -EAGAIN;
}

/*
 * The input method may return immediately, because input is
 * asynchronous. The data_done callback is invoked when the block is
 * full.
 */
static int fd_zio_input(struct zio_cset *cset)
{
	struct spec_fd *fd;
	fd = cset->zdev->private_data;

	/* Configure the device for input */
	if (!test_bit(FD_FLAG_DO_INPUT, &fd->flags)) {
		fd_writel(fd, FD_TSBCR_PURGE | FD_TSBCR_RST_SEQ, FD_REG_TSBCR);
		fd_writel(fd, FD_TSBCR_CHAN_MASK_W(1) | FD_TSBCR_ENABLE,
			  FD_REG_TSBCR);
		set_bit(FD_FLAG_DO_INPUT, &fd->flags);
	}
	/* Ready for input. If there's already something, return it now */
	if (fd_read_fifo(fd, cset->chan) == 0) {
		return 0; /* don't call data_done, let the caller do it */
	}
	/* Mark the active block is valid, and return EAGAIN */
	set_bit(FD_FLAG_INPUT_READY, &fd->flags);
	return -EAGAIN;
}

/*
 * The probe function receives a new zio_device, which is different from
 * what we allocated (that one is the "hardwre" device) but has the
 * same private data. So we make the link and return success.
 */
static int fd_zio_probe(struct zio_device *zdev)
{
	struct spec_fd *fd;

	/* link the new device from the fd structure */
	fd = zdev->private_data;
	fd->zdev = zdev;

	fd->tdc_attrs[FD_CSET_INDEX(FD_ATTR_TDC_OFFSET)] = \
		fd->calib.tdc_zero_offset;

	/* We don't have csets at this point, so don't do anything more */
	return 0;
}

/* Our sysfs operations to access internal settings */
static const struct zio_sysfs_operations fd_zio_s_op = {
	.conf_set = fd_zio_conf_set,
	.info_get = fd_zio_info_get,
};

/* We have 5 csets, since each output triggers separately */
static struct zio_cset fd_cset[] = {
	{
		SET_OBJECT_NAME("fd-input"),
		.raw_io =	fd_zio_input,
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
		.raw_io =	fd_zio_output,
		.n_chan =	1,
		.ssize =	4, /* FIXME: 0? */
		.flags =	ZIO_DIR_OUTPUT | ZCSET_TYPE_TIME,
		.zattr_set = {
			.ext_zattr = fd_zattr_output,
			.n_ext_attr = ARRAY_SIZE(fd_zattr_output),
		},
	},
	{
		SET_OBJECT_NAME("fd-ch2"),
		.raw_io =	fd_zio_output,
		.n_chan =	1,
		.ssize =	4, /* FIXME: 0? */
		.flags =	ZIO_DIR_OUTPUT | ZCSET_TYPE_TIME,
		.zattr_set = {
			.ext_zattr = fd_zattr_output,
			.n_ext_attr = ARRAY_SIZE(fd_zattr_output),
		},
	},
	{
		SET_OBJECT_NAME("fd-ch3"),
		.raw_io =	fd_zio_output,
		.n_chan =	1,
		.ssize =	4, /* FIXME: 0? */
		.flags =	ZIO_DIR_OUTPUT | ZCSET_TYPE_TIME,
		.zattr_set = {
			.ext_zattr = fd_zattr_output,
			.n_ext_attr = ARRAY_SIZE(fd_zattr_output),
		},
	},
	{
		SET_OBJECT_NAME("fd-ch4"),
		.raw_io =	fd_zio_output,
		.n_chan =	1,
		.ssize =	4, /* FIXME: 0? */
		.flags =	ZIO_DIR_OUTPUT | ZCSET_TYPE_TIME,
		.zattr_set = {
			.ext_zattr = fd_zattr_output,
			.n_ext_attr = ARRAY_SIZE(fd_zattr_output),
		},
	},
};

static struct zio_device fd_tmpl = {
	.owner =		THIS_MODULE,
	.preferred_trigger =	"user",
	.s_op =			&fd_zio_s_op,
	.cset =			fd_cset,
	.n_cset =		ARRAY_SIZE(fd_cset),
	.zattr_set = {
		.std_zattr= fd_zattr_dev_std,
		.ext_zattr= fd_zattr_dev,
		.n_ext_attr = ARRAY_SIZE(fd_zattr_dev),
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
	.probe = fd_zio_probe,
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

	fd->hwzdev = zio_allocate_device();
	if (IS_ERR(fd->hwzdev))
		return PTR_ERR(fd->hwzdev);

	/* Mandatory fields */
	fd->hwzdev->owner = THIS_MODULE;
	fd->hwzdev->private_data = fd;

	/* Our dev_id is bus+devfn */
	pdev = fd->spec->pdev;
	dev_id = (pdev->bus->number << 8) | pdev->devfn;

	err = zio_register_device(fd->hwzdev, "fd", dev_id);
	if (err) {
		zio_free_device(fd->hwzdev);
		return err;
	}

	setup_timer(&fd->fifo_timer, fd_timer_fn, (unsigned long)fd);
	if (fd_timer_period_ms)
		mod_timer(&fd->fifo_timer, jiffies + fd_timer_period_jiffies);
	return 0;
}

void fd_zio_exit(struct spec_fd *fd)
{
	del_timer_sync(&fd->fifo_timer);
	zio_unregister_device(fd->hwzdev);
	zio_free_device(fd->hwzdev);
}
