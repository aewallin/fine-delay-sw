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
#include "fine-delay.h"



/* Register and unregister are used to set up the template driver */
int fd_zio_register(void)
{
	/* FIXME */
	return 0;
}

void fd_zio_unregister(void)
{
	/* FIXME */
}

