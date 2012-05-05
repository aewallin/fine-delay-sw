/*
 * an input tool that measures performance
 * (almost a copy of fd-raw-input, even if code repetition is BAD)
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
#include <fcntl.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <linux/zio.h>
#include <linux/zio-user.h>
#include <fine-delay.h>

/*
 * Lazily, we ignore the "seconds" part, and assume we run at 5Hz minimum
 * so each picosecond count fits in 32 bits (0.4s)
 */
struct fd_perf {
	/* sequence is 16-bits, do the same here */
	uint16_t first, prev; /* sequence numbers: -1 == "not in a burst" */
	uint nev;
	uint64_t pico_tot, micro_tot;
	int64_t pico_prev, pico_min, pico_max, pico_avg;
	uint32_t micro_prev, micro_min, micro_max, micro_avg;
	int lost;
};

static void perf_clean(struct fd_perf *p)
{
	p->pico_tot = p->micro_tot = 0;
	p->pico_min = p->micro_min = ~0;
	p->pico_max = p->micro_max = 0;
	p->nev = p->lost = 0;
}

static void perf_one(struct fd_perf *p, uint32_t *a /* attrs */)
{
	struct timeval tv;
	int64_t pico, micro, diff;

	gettimeofday(&tv, NULL);
	pico = a[FD_ATTR_TDC_COARSE] * 8000LL
		+ (a[FD_ATTR_TDC_FRAC] << 12) / 8000;
	micro = tv.tv_usec;

	if (!p->nev) {
		p->first = a[FD_ATTR_TDC_SEQ];
		goto set_prev;
	}

	p->lost += (int16_t)(a[FD_ATTR_TDC_SEQ] - p->prev - 1);

	/* count hardware-reported pico */
	diff = pico - p->pico_prev;
	if (0) {
		printf("%lli = %f - %f\n", diff,
		       pico/1000000.0, p->pico_prev/1000000.0);
	}
	if (diff < 0)
		diff += 1000LL * 1000 * 1000 * 1000;
	if (diff < p->pico_min)
		p->pico_min = diff;
	if (diff > p->pico_max)
		p->pico_max = diff;
	p->pico_tot += diff;

	/* count software-reported micro */
	diff = micro - p->micro_prev;
	if (diff < 0)
		diff += 1000LL * 1000;
	if (diff < p->micro_min)
		p->micro_min = diff;
	if (diff > p->micro_max)
		p->micro_max = diff;
	p->micro_tot += diff;

set_prev:
	p->nev++;
	p->prev = a[FD_ATTR_TDC_SEQ];
	p->micro_prev = micro;
	p->pico_prev = pico;
}

static void perf_report_clean(struct fd_perf *p)
{
	if (p->prev < 0 || !p->nev)
		return;

	p->pico_avg = p->pico_tot / (p->nev - 1);
	p->micro_avg = p->micro_tot / (p->nev - 1);


	printf("%i pulses (%i lost)\n", p->nev, p->lost);
	printf("   hw: %llips (%fkHz) -- min %lli max %lli delta %lli\n",
	       p->pico_avg, 1000.0 * 1000 * 1000 / p->pico_avg,
	       p->pico_min, p->pico_max, p->pico_max - p->pico_min);
	printf("   sw: %ius (%fkHz) -- min %i max %i delta %i\n",
	       p->micro_avg, 1000.0 / p->micro_avg,
	       p->micro_min, p->micro_max, p->micro_max - p->micro_min);
	printf("\n");
	perf_clean(p);
}

#define MAXFD 16
struct zio_control ctrl;

int main(int argc, char **argv)
{
	glob_t glob_buf;
	int fd[MAXFD], seq[MAXFD];
	int i, j, maxfd = 0;
	fd_set allset, curset;
	struct timeval tout;
	struct fd_perf perf = {0,};
	int floatmode = 0;
	uint32_t *attrs;

	if (argc > 1 && !strcmp(argv[1], "-f")) {
		floatmode = 1;
		argv[1] = argv[0];
		argv++, argc--;
	}

	if (argc < 2) {
		/* rebuild argv using globbing */
		glob_buf.gl_offs = 1;
		/* "????" spits a trigraph warning: use "*" instead (bah!) */
		glob("/dev/zio-fd-*-0-0-ctrl", GLOB_DOOFFS, NULL, &glob_buf);
		glob("/dev/zio/zio-fd-*-0-0-ctrl",
		     GLOB_DOOFFS | GLOB_APPEND, NULL, &glob_buf);
		glob_buf.gl_pathv[0] = argv[0];
		argv = glob_buf.gl_pathv;
		argc = glob_buf.gl_pathc + glob_buf.gl_offs;
	}

	if (argc == 1) {
		fprintf(stderr, "%s: no arguments and no glob results\n",
		       argv[0]);
		exit(1);
	};

	if (argc > 2) {
		fprintf(stderr, "%s: too many devices, using only \"%s\"\n",
			argv[0], argv[1]);
		argc = 2;
		/* So the loops below are unchanged from fd-raw-input */
	};

	FD_ZERO(&allset);
	for (i = 1; i < 2; i++) {
		fd[i] = open(argv[i], O_RDONLY);
		if (fd[i] < 0) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[1],
			       strerror(errno));
			exit(1);
		}
		if (fd[i] > maxfd)
			maxfd = fd[i];
		FD_SET(fd[i], &allset);
		seq[i] = -1;
	}

	/* Ok, now wait for each of them to spit a timestamp */
	setlinebuf(stdout);
	perf_clean(&perf);
	while (1) {
		curset = allset;
		tout.tv_sec = 0;
		tout.tv_usec = 300*1000; /* After 300ms the burst is over */
		switch(select(maxfd + 1, &curset, NULL, NULL, &tout)) {
		case -1:
			if (errno == EINTR)
				continue;
			fprintf(stderr, "%s: select: %s\n", argv[0],
				strerror(errno));
			exit(1);
		case 0:
			perf_report_clean(&perf);
			continue;
		}


		for (i = 1; i < argc; i++) {
			if (!FD_ISSET(fd[i], &curset))
				continue;
			/* Ok, it's there: read and decode */
			j = read(fd[i], &ctrl, sizeof(ctrl));
			if (j != sizeof(ctrl)) {
				fprintf(stderr, "%s: read(): got %i not %i\n",
					argv[0], j, sizeof(ctrl));
				exit(1);
			}
			attrs = ctrl.attr_channel.ext_val;
			perf_one(&perf, attrs);
		}
	}
	return 0;
}
