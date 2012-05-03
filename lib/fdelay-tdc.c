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
#include <sys/select.h>

#include <linux/zio.h>
#include <linux/zio-user.h>
#define FDELAY_INTERNAL
#include "fdelay-lib.h"

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

static int __fdelay_open_tdc(struct __fdelay_board *b)
{
	char fname[128];
	if (b->fd[0] <= 0) {
		sprintf(fname, "%s-0-0-ctrl", b->devbase);
		b->fd[0] = open(fname, O_RDONLY | O_NONBLOCK);
	}
	return b->fd[0];
}

int fdelay_fileno_tdc(struct fdelay_board *userb)
{
	__define_board(b, userb);
	return __fdelay_open_tdc(b);
}


/* "read" behaves like the system call and obeys O_NONBLOCK */
int fdelay_read(struct fdelay_board *userb, struct fdelay_time *t, int n,
		       int flags)
{
	__define_board(b, userb);
	struct zio_control ctrl;
	uint32_t *attrs;
	int i, j, fd;
	fd_set set;

	fd = __fdelay_open_tdc(b);
	if (fd < 0)
		return fd; /* errno already set */

	for (i = 0; i < n;) {
		j = read(fd, &ctrl, sizeof(ctrl));
		if (j < 0 && errno != EAGAIN)
			return -1;
		if (j == sizeof(ctrl)) {
			/* one sample: pick it */
			attrs = ctrl.attr_channel.ext_val;
			t->utc = (uint64_t)attrs[FD_ATTR_TDC_UTC_H] << 32
				| attrs[FD_ATTR_TDC_UTC_L];
			t->coarse = attrs[FD_ATTR_TDC_COARSE];
			t->frac = attrs[FD_ATTR_TDC_FRAC];
			t->seq_id = attrs[FD_ATTR_TDC_SEQ];
			t->channel = attrs[FD_ATTR_TDC_CHAN];

			i++;
			continue;
		}
		if (j > 0) {
			errno = EIO;
			return -1;
		}
		/* so, it's EAGAIN: if we already got something, we are done */
		if (i)
			return i;
		/* EAGAIN at first sample */
		if (j < 0 && flags == O_NONBLOCK)
			return -1;

		/* So, first sample and blocking read. Wait.. */
		FD_ZERO(&set);
		FD_SET(fd, &set);
		if (select(fd+1, &set, NULL, NULL, NULL) < 0)
			return -1;
		continue;
	}
	return i;
}

/* "fread" behaves like stdio: it reads all the samples */
int fdelay_fread(struct fdelay_board *userb, struct fdelay_time *t, int n)
{
	int i, loop;

	for (i = 0; i < n; ) {
		loop = fdelay_read(userb, t + i, n - i, 0);
		if (loop < 0)
			return -1;
		i += loop;
	}
	return i;
}
