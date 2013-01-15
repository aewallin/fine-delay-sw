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
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/io.h>

#include <linux/fmc.h>
#include <linux/fmc-sdb.h>

#include "fine-delay.h"
#include "hw/fd_main_regs.h"

/* Module parameters */
static int fd_verbose = 0;
module_param_named(verbose, fd_verbose, int, 0444);

static struct fmc_driver fd_drv; /* forward declaration */
FMC_PARAM_BUSID(fd_drv);
FMC_PARAM_GATEWARE(fd_drv);

static int fd_show_sdb;
module_param_named(show_sdb, fd_show_sdb, int, 0444);

/* FIXME: add parameters "file=" and "wrc=" like wr-nic-core does */

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
static void fd_do_reset(struct fd_dev *fd, int hw_reset)
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
int fd_gpio_defaults(struct fd_dev *fd)
{
	fd_gpio_dir(fd, FD_GPIO_TRIG_INTERNAL, FD_GPIO_OUT);
	fd_gpio_set(fd, FD_GPIO_TRIG_INTERNAL);

	fd_gpio_set(fd, FD_GPIO_OUTPUT_MASK);
	fd_gpio_dir(fd, FD_GPIO_OUTPUT_MASK, FD_GPIO_OUT);

	fd_gpio_dir(fd, FD_GPIO_TERM_EN, FD_GPIO_OUT);
	fd_gpio_clr(fd, FD_GPIO_TERM_EN);
	return 0;
}

int fd_reset_again(struct fd_dev *fd)
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
	int (*init)(struct fd_dev *);
	void (*exit)(struct fd_dev *);
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

/* probe and remove are called by the FMC bus core */
int fd_probe(struct fmc_device *fmc)
{
	struct fd_modlist *m;
	struct fd_dev *fd;
	struct device *dev = fmc->hwdev;
	char *fwname;
	int i, index, ret;

	fd = kzalloc(sizeof(*fd), GFP_KERNEL);
	if (!fd) {
		dev_err(dev, "can't allocate device\n");
		return -ENOMEM;
	}

	index = fmc->op->validate(fmc, &fd_drv);
	if (index < 0) {
		dev_info(dev, "not using \"%s\" according to "
			 "modparam\n", KBUILD_MODNAME);
		return -ENODEV;
	}

	fwname = FDELAY_GATEWARE_NAME;
	if (fd_drv.gw_n)
		fwname = ""; /* ->reprogram will pick from module parameter */
	ret = fmc->op->reprogram(fmc, &fd_drv, fwname);
	if (ret < 0) {
		if (ret == -ESRCH) {
			dev_info(dev, "%s: no gateware at index %i\n",
				 KBUILD_MODNAME, index);
			return -ENODEV;
		}
		return ret; /* other error: pass over */
	}

	/* All our FPGA images are expected to have SDB at offset 0 */
	if (fmc_readl(fmc, 0) != 0x5344422d) {
		dev_err(dev, "Can't find SDB magic (got 0x%x)\n",
			fmc_readl(fmc, 0));
		ret = -ENODEV;
		goto out;
	}
	dev_info(dev, "Gateware successfully loaded\n");

	if ( (ret = fmc_scan_sdb_tree(fmc, 0)) < 0) {
		dev_err(dev, "scan fmc failed %i\n", ret);
		goto out;
	}
	if (fd_show_sdb)
		fmc_show_sdb_tree(fmc);

	spin_lock_init(&fd->lock);
	fmc->mezzanine_data = fd;
	fd->fmc = fmc;
	fd->verbose = fd_verbose;
	fd->calib = fd_default_calib;

	/* Check the binary is there */
	if (fd_readl(fd, FD_REG_IDR) != FD_MAGIC_FPGA) {
		dev_err(dev, "wrong gateware\n");
		return -ENODEV;
	} else {
		dev_info(dev, "%s: initializing\n", KBUILD_MODNAME);
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

	/* Finally, enable the input emgine */
	ret = fd_irq_init(fd);
	if (ret < 0)
		goto err;

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
out:
	return ret;
}

int fd_remove(struct fmc_device *fmc)
{
	struct fd_modlist *m;
	struct fd_dev *fd = fmc->mezzanine_data;
	int i = ARRAY_SIZE(mods);

	if (!test_bit(FD_FLAG_INITED, &fd->flags)) /* FIXME: ditch this */
		return 0; /* No init, no exit */

	fd_irq_exit(fd);
	while (--i >= 0) {
		m = mods + i;
		if (m->exit)
			m->exit(fd);
	}
	return 0;
}

static struct fmc_driver fd_drv = {
	.version = FMC_VERSION,
	.driver.name = KBUILD_MODNAME,
	.probe = fd_probe,
	.remove = fd_remove,
	/* no table, as the current match just matches everything */
};

static int fd_init(void)
{
	int ret;

	ret = fd_zio_register();
	if (ret < 0)
		return ret;
	ret = fmc_driver_register(&fd_drv);
	if (ret < 0) {
		fd_zio_unregister();
		return ret;
	}
	return 0;
}

static void fd_exit(void)
{
	fmc_driver_unregister(&fd_drv);
	fd_zio_unregister();
}

module_init(fd_init);
module_exit(fd_exit);

MODULE_LICENSE("GPL and additional rights"); /* LGPL */
