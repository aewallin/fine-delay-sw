/*
 * TDC-related functions
 *
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2 as published by the Free Software Foundation or, at your
 * option, any later version.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <linux/zio.h>
#include <linux/zio-user.h>
#include <fine-delay.h>
#define FDELAY_INTERNAL
#include "fdelay.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static int config_mask =
	FD_TDCF_DISABLE_INPUT |
	FD_TDCF_DISABLE_TSTAMP |
	FD_TDCF_TERM_50;

int fdelay_set_config_tdc(struct fdelay_board *userb, int flags)
{
	__define_board(b, userb);
	uint32_t val;

	if (flags & ~config_mask) {
		errno = EINVAL;
		return -1;
	}
	val = flags;
	return fdelay_sysfs_set(b, "fd-input/flags", &val);
}

int fdelay_get_config_tdc(struct fdelay_board *userb)
{
	__define_board(b, userb);
	uint32_t val;
	int ret;

	ret = fdelay_sysfs_get(b, "fd-input/flags", &val);
	if (ret) return ret;
	return val;
}
