/*
 * The "official" fine-delay API
 *
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2 as published by the Free Software Foundation or, at your
 * option, any later version.
 */
#ifndef __FDELAY_H__
#define __FDELAY_H__

/* Opaque data type used as token */
struct fdelay_board;


/* Please see the manual for the meaning of arguments and return values */
extern int fdelay_init(void);
extern void fdelay_exit(void);

extern struct fdelay_board *fdelay_open(int offset, int dev_id);
extern int fdelay_close(struct fdelay_board *);

#ifdef FDELAY_INTERNAL /* Libray users should ignore what follows */

/* Internal structure */
struct __fdelay_board {
	int dev_id;
	char *devbase;
	char *sysbase;
	int fd[5]; /* The 5 channels */
};

static inline int fdelay_is_verbose(void)
{
	return getenv("FDELAY_LIB_VERBOSE") != 0;
}

#define __define_board(b, ub)	struct __fdelay_board *b = (void *)(ub)

#endif /* FDELAY_INTERNAL */
#endif /* __FDELAY_H__ */
