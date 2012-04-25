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
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/io.h>

#include <linux/zio.h>
#include <linux/zio-buffer.h>
#include <linux/zio-trigger.h>
#include "fine-delay.h"

/* FIXME: all the attributes */
DEFINE_ZATTR_STD(ZDEV, fd_zattr_dev) = {
	ZATTR_REG(zdev, ZATTR_NBITS, S_IRUGO, 0, 1), /* 1 bit. bah... */
};

static int fd_output(struct zio_cset *cset)
{
	/* FIXME: use chan->active_block */

	return 0; /* Already done, as the trigger is hardware */
}

static int fd_input(struct zio_cset *cset)
{
	/* FIXME: fill chan->active_block */

	return 0; /* Already done, as the trigger is hardware */
}

/* We have 5 csets, since each output triggers separately */
static struct zio_cset fd_cset[] = {
	{
		SET_OBJECT_NAME("fd-input"),
		.raw_io =	fd_input,
		.n_chan =	1,
		.ssize =	1, /* FIXME: 0? */
		.flags =	ZIO_DIR_INPUT | ZCSET_TYPE_TIME,
	},
	{
		SET_OBJECT_NAME("fd-ch1"),
		.raw_io =	fd_output,
		.n_chan =	1,
		.ssize =	1, /* FIXME: 0? */
		.flags =	ZIO_DIR_OUTPUT | ZCSET_TYPE_TIME,
	},
	{
		SET_OBJECT_NAME("fd-ch2"),
		.raw_io =	fd_output,
		.n_chan =	1,
		.ssize =	1, /* FIXME: 0? */
		.flags =	ZIO_DIR_OUTPUT | ZCSET_TYPE_TIME,
	},
	{
		SET_OBJECT_NAME("fd-ch3"),
		.raw_io =	fd_output,
		.n_chan =	1,
		.ssize =	1, /* FIXME: 0? */
		.flags =	ZIO_DIR_OUTPUT | ZCSET_TYPE_TIME,
	},
	{
		SET_OBJECT_NAME("fd-ch4"),
		.raw_io =	fd_output,
		.n_chan =	1,
		.ssize =	1, /* FIXME: 0? */
		.flags =	ZIO_DIR_OUTPUT | ZCSET_TYPE_TIME,
	},
};

static struct zio_device fd_tmpl = {
	.owner =		THIS_MODULE,
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

	/* FIXME: register a trigger too */

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

	fd->zdev = zio_allocate_device();
	if (IS_ERR(fd->zdev))
		return PTR_ERR(fd->zdev);
	fd->zdev->owner = THIS_MODULE;
	err = zio_register_device(fd->zdev, "fd", 0);
	if (err) {
		zio_free_device(fd->zdev);
		return err;
	}
	return 0;
}

void fd_zio_exit(struct spec_fd *fd)
{
	zio_unregister_device(fd->zdev);
	zio_free_device(fd->zdev);
}
