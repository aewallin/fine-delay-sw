/*
 * SPI access to fine-delay internals
 *
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2 as published by the Free Software Foundation or, at your
 * option, any later version.
 */

#include <linux/io.h>
#include <linux/time.h>
#include <linux/spinlock.h>
#include "fine-delay.h"
#include "hw/fd_main_regs.h"

/* If fd_time is not null, use it. if ts is not null, use it, else current */
int fd_time_set(struct spec_fd *fd, struct fd_time *t, struct timespec *ts)
{
	uint32_t tcr, gcr;
	unsigned long flags;
	struct timespec localts;

	spin_lock_irqsave(&fd->lock, flags);

	gcr = fd_readl(fd, FD_REG_GCR);
	fd_writel(fd, 0, FD_REG_GCR); /* zero the GCR while setting time */
	if (t) {
		fd_writel(fd, t->utc >> 32, FD_REG_TM_SECH);
		fd_writel(fd, t->utc & 0xffffffff, FD_REG_TM_SECL);
		fd_writel(fd, t->coarse, FD_REG_TM_CYCLES);
	} else {
		if (!ts) {
			/* no caller-provided time: use Linux timer */
			ts = &localts;
			getnstimeofday(ts);
		}
		fd_writel(fd, GET_HI32(ts->tv_sec), FD_REG_TM_SECH);
		fd_writel(fd, (int32_t)ts->tv_sec, FD_REG_TM_SECL);
		fd_writel(fd, ts->tv_nsec >> 3, FD_REG_TM_CYCLES);
	}

	tcr = fd_readl(fd, FD_REG_TCR);
	fd_writel(fd, tcr | FD_TCR_SET_TIME, FD_REG_TCR);
	fd_writel(fd, gcr, FD_REG_GCR); /* Restore GCR */

	spin_unlock_irqrestore(&fd->lock, flags);
	return 0;
}

/* If fd_time is not null, use it. Otherwise use ts */
int fd_time_get(struct spec_fd *fd, struct fd_time *t, struct timespec *ts)
{
	uint32_t tcr, h, l, c;
	unsigned long flags;

	spin_lock_irqsave(&fd->lock, flags);
	tcr = fd_readl(fd, FD_REG_TCR);
	fd_writel(fd, tcr | FD_TCR_CAP_TIME, FD_REG_TCR);
	h = fd_readl(fd, FD_REG_TM_SECH);
	l = fd_readl(fd, FD_REG_TM_SECL);
	c = fd_readl(fd, FD_REG_TM_CYCLES);
	spin_unlock_irqrestore(&fd->lock, flags);

	if (t) {
		t->utc = ((uint64_t)h << 32) | l;
		t->coarse = c;
	}
	if (ts) {
		ts->tv_sec = ((uint64_t)h << 32) | l;
		ts->tv_nsec = c * 8;
	}
	return 0;
}

int fd_time_init(struct spec_fd *fd)
{
	struct timespec ts = {0,0};

	/* Set the time to zero, so internal stuff resyncs */
	return fd_time_set(fd, NULL, &ts);
}

void fd_time_exit(struct spec_fd *fd)
{
	/* nothing to do */
}

