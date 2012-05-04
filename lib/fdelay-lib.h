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
#include <stdint.h>
#include <fine-delay.h>

/* Opaque data type used as token */
struct fdelay_board;

struct fdelay_time {
	uint64_t utc;
	uint32_t coarse;
	uint32_t frac;
	uint32_t seq_id;
	uint32_t channel;
};

/* The structure used for pulse generation */
struct fdelay_pulse {
	/* FD_OUT_MODE_DISABLED, FD_OUT_MODE_DELAY, FD_OUT_MODE_PULSE */
	int mode;
	/* -1 == infinite */
	int rep;

	struct fdelay_time start;
	struct fdelay_time end;
	struct fdelay_time loop;
};

/* An alternative structure, internally converted to the previous one */
struct fdelay_pulse_ps {
	int mode;
	int rep;
	struct fdelay_time start;
	uint64_t length;
	uint64_t period;
};

/*
 * Please see the manual for the meaning of arguments and return values
 */

extern int fdelay_init(void);
extern void fdelay_exit(void);

extern struct fdelay_board *fdelay_open(int offset, int dev_id);
extern int fdelay_close(struct fdelay_board *);

extern int fdelay_set_time(struct fdelay_board *b, struct fdelay_time *t);
extern int fdelay_get_time(struct fdelay_board *b, struct fdelay_time *t);
extern int fdelay_set_host_time(struct fdelay_board *b);

extern int fdelay_set_config_tdc(struct fdelay_board *b, int flags);
extern int fdelay_get_config_tdc(struct fdelay_board *b);

extern int fdelay_fread(struct fdelay_board *b, struct fdelay_time *t, int n);
extern int fdelay_fileno_tdc(struct fdelay_board *b);
extern int fdelay_read(struct fdelay_board *b, struct fdelay_time *t, int n,
		       int flags);

extern void fdelay_pico_to_time(uint64_t *pico, struct fdelay_time *time);
extern void fdelay_time_to_pico(struct fdelay_time *time, uint64_t *pico);

extern int fdelay_config_pulse(struct fdelay_board *b,
			       int channel, struct fdelay_pulse *pulse);
extern int fdelay_config_pulse_ps(struct fdelay_board *b,
				  int channel, struct fdelay_pulse_ps *ps);



#ifdef FDELAY_INTERNAL /* Libray users should ignore what follows */

/* Internal structure */
struct __fdelay_board {
	int dev_id;
	char *devbase;
	char *sysbase;
	int fdc[5]; /* The 5 control channels */
	int fdd[5]; /* The 5 data channels */
};

static inline int fdelay_is_verbose(void)
{
	return getenv("FDELAY_LIB_VERBOSE") != 0;
}

#define __define_board(b, ub)	struct __fdelay_board *b = (void *)(ub)

/* These two from ../tools/fdelay-raw.h, used internally */
static inline int __fdelay_sysfs_get(char *path, uint32_t *resp)
{
	FILE *f = fopen(path, "r");

	if (!f)
		return -1;
	if (fscanf(f, "%i", resp) != 1) {
		fclose(f);
		errno = EINVAL;
		return -1;
	}
	fclose(f);
	return 0;
}

static inline int __fdelay_sysfs_set(char *path, uint32_t *value)
{
	FILE *f = fopen(path, "w");

	if (!f)
		return -1;
	if (fprintf(f, "%i\n", *value) < 2) {
		fclose(f);
		errno = EINVAL;
		return -1;
	}
	fclose(f);
	return 0;
}

/* And these two for the board structure */
static inline int fdelay_sysfs_get(struct __fdelay_board *b, char *name,
			       uint32_t *resp)
{
	char pathname[128];

	sprintf(pathname, "%s/%s", b->sysbase, name);
	return __fdelay_sysfs_get(pathname, resp);
}

static inline int fdelay_sysfs_set(struct __fdelay_board *b, char *name,
			       uint32_t *value)
{
	char pathname[128];

	sprintf(pathname, "%s/%s", b->sysbase, name);
	return __fdelay_sysfs_set(pathname, value);
}

static inline int __fdelay_command(struct __fdelay_board *b, uint32_t cmd)
{
	return fdelay_sysfs_set(b, "command", &cmd);
}



#endif /* FDELAY_INTERNAL */
#endif /* __FDELAY_H__ */
