/*
 * output-related functions
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

void fdelay_pico_to_time(uint64_t *pico, struct fdelay_time *time)
{
	uint64_t p = *pico;

	time->utc = p / (1000ULL * 1000ULL * 1000ULL * 1000ULL);
	p %= (1000ULL * 1000ULL * 1000ULL * 1000ULL);
	time->coarse = p / 8000;
	p %= 8000;
	time->frac = p * 4096 / 8000;
}

void fdelay_time_to_pico(struct fdelay_time *time, uint64_t *pico)
{
	uint64_t p;

	p = time->frac * 8000 / 4096;
	p += time->coarse * 8000;
	p += time->utc * (1000ULL * 1000ULL * 1000ULL * 1000ULL);
	*pico = p;
}

extern int fdelay_config_pulse(struct fdelay_board *b,
			       int channel, struct fdelay_pulse *pulse);

/* The "pulse_ps" function relies on the previous one */
int fdelay_config_pulse_ps(struct fdelay_board *b,
			   int channel, struct fdelay_pulse_ps *ps)
{
	struct fdelay_pulse p;

	p.mode = ps->mode;
	p.rep = ps->rep;
	p.start = ps->start;
	p.end = ps->start;
	// fdelay_add_ps(&p.end, ps->length);
	fdelay_pico_to_time(&ps->period, &p.loop);
	return fdelay_config_pulse(b, channel, &p);
}



