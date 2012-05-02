/*
 * Initializing and cleaning up the fdelay library
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
#include <glob.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <linux/zio.h>
#include <linux/zio-user.h>
#include <fine-delay.h>
#define FDELAY_INTERNAL
#include "fdelay.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static struct __fdelay_board *fd_boards;
static int fd_nboards;

/* Init the library: return the number of boards found */
int fdelay_init(void)
{
	glob_t glob_dev, glob_sys;
	struct __fdelay_board *b;
	int i, j;
	uint32_t v;

	/* Look for boards in /dev: old and new pathnames: only one matches */
	glob("/dev/zio-fd-*-0-0-ctrl", 0, NULL, &glob_dev);
	glob("/dev/zio/zio-fd-*-0-0-ctrl", GLOB_APPEND, NULL, &glob_dev);

	/* And look in /sys as well */
        glob("/sys/bus/zio/devices/zio-fd-*",0 , NULL, &glob_sys);
	assert(glob_dev.gl_pathc == glob_sys.gl_pathc);

	/* Allocate as needed */
	fd_nboards = glob_dev.gl_pathc;
	if (!fd_nboards) {
		fd_boards = NULL;
		return 0;
	}
	fd_boards = calloc(glob_dev.gl_pathc, sizeof(fd_boards[0]));
	if (!fd_boards) {
		globfree(&glob_dev);
		globfree(&glob_sys);
		return -1;
	}

	for (i = 0, b = fd_boards; i < fd_nboards; i++, b++) {
		b->sysbase = strdup(glob_sys.gl_pathv[i]);
		b->devbase = strdup(glob_dev.gl_pathv[i]);
		/* trim the "-0-0-ctrl" at the end */
		b->devbase[strlen(b->devbase) - strlen("-0-0-ctrl")] = '\0';
		/* extract dev_id */
		sscanf(b->sysbase, "%*[^f]fd-%x", &b->dev_id);
		for (j = 0; j < ARRAY_SIZE(b->fd); j++)
			b->fd[j] = -1;
		if (fdelay_is_verbose()) {
			fprintf(stderr, "%s: %04x %s %s\n", __func__,
				b->dev_id, b->sysbase, b->devbase);
		}
	}
	globfree(&glob_dev);
	globfree(&glob_sys);

	/* Now, if at least one board is there, check the version */
	if (fd_nboards == 0)
		return 0;

	if (fdelay_sysfs_get(fd_boards, "version", &v) < 0)
		return -1;
	if (v != FDELAY_VERSION) {
		fprintf(stderr, "%s: version mismatch, lib(%i) != drv(%i)\n",
			__func__, FDELAY_VERSION, v);
		errno = EIO;
		return -1;
	}
	return fd_nboards;
}

/* Free and check */
void fdelay_exit(void)
{
	struct __fdelay_board *b;
	int i, j, err;

	for (i = 0, err = 0, b = fd_boards; i < fd_nboards; i++, b++) {
		for (j = 0; j < ARRAY_SIZE(b->fd); j++) {
			if (b->fd[j] >= 0) {
				close(b->fd[j]);
				err++;
			}
		}
		if (err)
			fprintf(stderr, "%s: device %s was still open\n",
				__func__, b->devbase);
		free(b->sysbase);
		free(b->devbase);
	}
	if(fd_nboards)
		free(fd_boards);
}

/* Open one specific device. -1 arguments mean "not installed" */
struct fdelay_board *fdelay_open(int offset, int dev_id)
{
	struct __fdelay_board *b = NULL;
	int i;

	if (offset >= fd_nboards) {
		errno = ENODEV;
		return NULL;
	}
	if (offset >= 0) {
		b = fd_boards + offset;
		if (dev_id >= 0 && dev_id != b->dev_id) {
			errno = EINVAL;
			return NULL;
		}
		return (void *)b;
	}
	if (dev_id < 0) {
		errno = EINVAL;
		return NULL;
	}
	for (i = 0, b = fd_boards; i < fd_nboards; i++, b++)
		if (b->dev_id == dev_id)
			return (void *)b;
	errno = ENODEV;
	return NULL;
}

int fdelay_close(struct fdelay_board *userb)
{
	__define_board(b, userb);
	int j;

	for (j = 0; j < ARRAY_SIZE(b->fd); j++)
		if (b->fd[j] >= 0)
			close(b->fd[j]);
	return 0;

}


