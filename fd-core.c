/*
 * core fine-delay driver (i.e., init and exit of the subsystems)
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
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/io.h>

#include "spec.h"
#include "fine-delay.h"
#include "hw/fd_main_regs.h"

/* Module parameters */
static int fd_regs_offset = FD_REGS_OFFSET;
module_param_named(regs, fd_regs_offset, int, 0444);

static int fd_verbose = 0;
module_param_named(verbose, fd_verbose, int, 0444);

/* This is pre-set at load time (data by Tomasz) */
static struct fd_calib fd_default_calib = {
	.frr_poly = {
		[0] =     -165202LL,
		[1] =     -29825595LL,
		[2] = 3801939743082LL,
	},
	.tdc_zero_offset = 35600 -63100,
	.atmcr_val =  4 | (1500 << 4),
	.adsfr_val = 56648,
	.acam_start_offset = 10000,
	.zero_offset = {
		14400, 14400, 14400, 14400
	},
};

/* The reset function (by Tomasz) */
static void fd_do_reset(struct spec_fd *fd, int hw_reset)
{
	if (hw_reset) {
		fd_writel(fd, FD_RSTR_LOCK_W(0xdead) | FD_RSTR_RST_CORE_MASK,
		       FD_REG_RSTR);
		udelay(10000);
		fd_writel(fd, FD_RSTR_LOCK_W(0xdead) | FD_RSTR_RST_CORE_MASK
		       | FD_RSTR_RST_FMC_MASK, FD_REG_RSTR);
		/* TPS3307 supervisor needs time to de-assert master reset */
		msleep(600);
		return;
	}

	fd_writel(fd, FD_RSTR_LOCK_W(0xdead) | FD_RSTR_RST_FMC_MASK,
		  FD_REG_RSTR);
	udelay(1000);
	fd_writel(fd, FD_RSTR_LOCK_W(0xdead) | FD_RSTR_RST_FMC_MASK
	       | FD_RSTR_RST_CORE_MASK, FD_REG_RSTR);
	udelay(1000);
}

/* Some init procedures to be intermixed with subsystems */
int fd_gpio_defaults(struct spec_fd *fd)
{
	fd_gpio_dir(fd, FD_GPIO_TRIG_INTERNAL, FD_GPIO_OUT);
	fd_gpio_set(fd, FD_GPIO_TRIG_INTERNAL);

	fd_gpio_set(fd, FD_GPIO_OUTPUT_MASK);
	fd_gpio_dir(fd, FD_GPIO_OUTPUT_MASK, FD_GPIO_OUT);

	fd_gpio_dir(fd, FD_GPIO_TERM_EN, FD_GPIO_OUT);
	fd_gpio_clr(fd, FD_GPIO_TERM_EN);
	return 0;
}

int fd_reset_again(struct spec_fd *fd)
{
	unsigned long j;

	/* Reset the FD core once we have proper reference/TDC clocks */
	fd_do_reset(fd, 0 /* not hw */);

	j = jiffies + 2 * HZ;
	while (time_before(jiffies, j)) {
		if (fd_readl(fd, FD_REG_GCR) & FD_GCR_DDR_LOCKED)
			break;
		msleep(10);
	}
	if (time_after_eq(jiffies, j)) {
		pr_err("%s: timeout waiting for GCR lock bit\n", __func__);
		return -EIO;
	}

	fd_do_reset(fd, 0 /* not hw */);
	return 0;
}

/* This structure lists the various subsystems */
struct fd_modlist {
	char *name;
	int (*init)(struct spec_fd *);
	void (*exit)(struct spec_fd *);
};


#define SUBSYS(x) { #x, fd_ ## x ## _init, fd_ ## x ## _exit }
static struct fd_modlist mods[] = {
	SUBSYS(spi),
	SUBSYS(gpio),
	SUBSYS(pll),
	SUBSYS(onewire),
	{"gpio-default", fd_gpio_defaults},
	{"reset-again", fd_reset_again},
	SUBSYS(acam),
	SUBSYS(time),
	SUBSYS(i2c),
	SUBSYS(zio),
};

/* probe and remove are called by fd-spec.c */
int fd_probe(struct spec_dev *dev)
{
	struct fd_modlist *m;
	struct spec_fd *fd;
	int i, ret;

	pr_debug("%s\n",__func__);
	fd = kzalloc(sizeof(*fd), GFP_KERNEL);
	if (!fd) {
		pr_err("%s: can't allocate device\n", __func__);
		return -ENOMEM;
	}
	spin_lock_init(&fd->lock);
	dev->sub_priv = fd;
	fd->spec = dev;
	fd->base = dev->remap[0];
	fd->regs = fd->base + fd_regs_offset;
	fd->ow_regs = fd->regs + 0x500;
	fd->verbose = fd_verbose;
	fd->calib = fd_default_calib;

	/* Check the binary is there */
	if (fd_readl(fd, FD_REG_IDR) != FD_MAGIC_FPGA) {
		pr_err("%s: card at %04x:%04x (regs @ 0x%x): wrong gateware\n",
		       __func__, dev->pdev->bus->number, dev->pdev->devfn,
			fd_regs_offset);
		return -ENODEV;
	} else {
		pr_info("%s: card at %04x:%04x (regs @ 0x%x): initializing\n",
		       __func__, dev->pdev->bus->number, dev->pdev->devfn,
			fd_regs_offset);
	}

	/* First, hardware reset */
	fd_do_reset(fd, 1);

	/* init all subsystems */
	for (i = 0, m = mods; i < ARRAY_SIZE(mods); i++, m++) {
		pr_debug("%s: Calling init for \"%s\"\n", __func__,
			 m->name);
		ret = m->init(fd);
		if (ret < 0) {
			pr_err("%s: error initializing %s\n", __func__,
				 m->name);
			goto err;
		}
	}

	/* Finally, enable the input */
	fd_writel(fd, FD_GCR_INPUT_EN, FD_REG_GCR);

	if (0) {
		struct timespec ts1, ts2, ts3;
		/* Temporarily, test the time stuff */
		fd_time_set(fd, NULL, NULL);
		fd_time_get(fd, NULL, &ts1);
		msleep(100);
		fd_time_get(fd, NULL, &ts2);
		getnstimeofday(&ts3);
		printk("%li.%li\n%li.%li\n%li.%li\n",
		       ts1.tv_sec, ts1.tv_nsec,
		       ts2.tv_sec, ts2.tv_nsec,
		       ts3.tv_sec, ts3.tv_nsec);
	}
	set_bit(FD_FLAG_INITED, &fd->flags);
	return 0;

err:
	while (--m, --i >= 0)
		if (m->exit)
			m->exit(fd);
	return ret;
}

void fd_remove(struct spec_dev *dev)
{
	struct fd_modlist *m;
	struct spec_fd *fd = dev->sub_priv;
	int i = ARRAY_SIZE(mods);

	if (!test_bit(FD_FLAG_INITED, &fd->flags))
		return; /* No init, no exit */

	pr_debug("%s\n",__func__);
	while (--i >= 0) {
		m = mods + i;
		if (m->exit)
			m->exit(fd);
	}
}

static int fd_init(void)
{
	int ret;

	pr_debug("%s\n",__func__);
	ret = fd_zio_register();
	if (ret < 0)
		return ret;
	ret = fd_spec_init();
	if (ret < 0) {
		fd_zio_unregister();
		return ret;
	}
	return 0;
}

static void fd_exit(void)
{
	fd_spec_exit();
	fd_zio_unregister();
}

module_init(fd_init);
module_exit(fd_exit);

MODULE_LICENSE("GPL and additional rights"); /* LGPL */
