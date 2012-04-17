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
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/io.h>

#include "spec.h"
#include "fine-delay.h"
#include "hw/fd_main_regs.h"

/* The reset function (by Tomasz) */
static void fd_do_reset(struct spec_fd *fd, int hw_reset)
{
	if (hw_reset) {
		writel(FD_RSTR_LOCK_W(0xdead) | FD_RSTR_RST_CORE_MASK,
		       fd->regs + FD_REG_RSTR);
		udelay(10000);
		writel(FD_RSTR_LOCK_W(0xdead) | FD_RSTR_RST_CORE_MASK
		       | FD_RSTR_RST_FMC_MASK,
		       fd->regs + FD_REG_RSTR);
		/* TPS3307 supervisor needs time to de-assert master reset */
		msleep(600);
		return;
	}

	writel(FD_RSTR_LOCK_W(0xdead) | FD_RSTR_RST_FMC_MASK,
	       fd->regs + FD_REG_RSTR);
	udelay(1000);
	writel(FD_RSTR_LOCK_W(0xdead) | FD_RSTR_RST_FMC_MASK
	       | FD_RSTR_RST_CORE_MASK,
	       fd->regs + FD_REG_RSTR);
	udelay(1000);
}



/* This structure lists the various subsystems */
struct modlist {
	char *name;
	int (*init)(struct spec_fd *);
	void (*exit)(struct spec_fd *);
};


#define M(x) { #x, fd_ ## x ## _init, fd_ ## x ## _exit }
static struct modlist mods[] = {
	M(spi),
	M(gpio),
	M(pll),
	//M(w1),
	//M(i2c),
	//M(acam),
	//M(zio),
};
#undef M

/* probe and remove are called by fd-spec.c */
int fd_probe(struct spec_dev *dev)
{
	struct modlist *m;
	struct spec_fd *fd;
	int i, ret;

	pr_debug("%s\n",__func__);
	fd = kzalloc(sizeof(*fd), GFP_KERNEL);
	if (!fd) {
		pr_err("%s: can't allocate device\n", __func__);
		return -ENOMEM;
	}
	dev->sub_priv = fd;
	fd->spec = dev;
	fd->base = dev->remap[0];
	fd->regs = fd->base + FD_REGS_OFFSET;

	/* First, hardware reset */
	fd_do_reset(fd, 1);

	/* init all subsystems */
	for (i = 0, m = mods; i < ARRAY_SIZE(mods); i++, m++) {
		ret = m->init(fd);
		if (ret < 0) {
			pr_err("%s: error initializing %s\n", __func__,
				 m->name);
			goto err;
		}
	}
	return 0;

err:
	while (--m, --i >= 0)
		m->exit(fd);
	return ret;
}

void fd_remove(struct spec_dev *dev)
{
	struct modlist *m;
	int i = ARRAY_SIZE(mods);

	pr_debug("%s\n",__func__);
	while (--i >= 0) {
		m = mods + i;
		m->exit(dev->sub_priv);
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
