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
	uint32_t tcr;
	unsigned long flags;
	struct timespec localts;

	spin_lock_irqsave(&fd->lock, flags);

	fd_writel(fd, 0, FD_REG_GCR);
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
		if (BITS_PER_LONG > 32)
			fd_writel(fd, ts->tv_sec >> 32, FD_REG_TM_SECH);
		else
			fd_writel(fd, 0, FD_REG_TM_SECH);
		fd_writel(fd, (int32_t)ts->tv_sec, FD_REG_TM_SECL);
		fd_writel(fd, ts->tv_nsec >> 3, FD_REG_TM_CYCLES);
	}

	tcr = fd_readl(fd, FD_REG_TCR);
	fd_writel(fd, tcr | FD_TCR_SET_TIME, FD_REG_TCR);

	spin_unlock_irqrestore(&fd->lock, flags);
	return 0;
}

/* If fd_time is not null, use it. Otherwise use ts */
int fd_time_get(struct spec_fd *fd, struct fd_time *t, struct timespec *ts)
{
	uint32_t h1, l1, h2, l2, c;
	unsigned long flags;

	spin_lock_irqsave(&fd->lock, flags);

	/* get the tuple. If inconsistent re-read the high part */
	h1 = fd_readl(fd, FD_REG_TM_SECH);
	l1 = fd_readl(fd, FD_REG_TM_SECL);
	c  = fd_readl(fd, FD_REG_TM_CYCLES);
	h2 = fd_readl(fd, FD_REG_TM_SECH);
	l2 = fd_readl(fd, FD_REG_TM_SECL);
	if (h2 != h1 || l2 != l1) {
		c  = fd_readl(fd, FD_REG_TM_CYCLES);
		h1 = h2;
		l1 = l2;
	}
	spin_unlock_irqrestore(&fd->lock, flags);
	printk("got %i %i %i\n", h1, l1, c);
	if (t) {
		t->utc = ((uint64_t)h1 << 32) | l1;
		t->coarse = c;
	}
	if (ts) {
		ts->tv_sec = ((uint64_t)h1 << 32) | l1;
		ts->tv_nsec = c * 8;
	}
	return 0;
}

int fd_time_init(struct spec_fd *fd)
{
	/* nothing to do */
	return 0;
}

void fd_time_exit(struct spec_fd *fd)
{
	/* nothing to do */
}

