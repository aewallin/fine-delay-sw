/*
 * an input tools that accesses the ZIO device
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
#include <time.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <linux/zio.h>
#include <linux/zio-user.h>
#include <fine-delay.h>

enum {
	MODE_HEX = 1,
	MODE_FLOAT = 2,
	MODE_PICO = 4
};

int expect;
int show_time;

void event(uint32_t *a, char *name, int *seq, int modemask, long double *t1,
	uint64_t *p1)
{
	int sequence = a[FD_ATTR_TDC_SEQ];
	long double t2;
	int64_t p2, delta;
	static int64_t guess;

	if (*seq != -1) {
		if (sequence  != ((*seq + 1) & 0xffff)) {
			printf("%s: LOST %i events\n", name,
			       (sequence - (*seq + 1)) & 0xffff);
			*t1 = 0.0;
			*p1 = 0LL;
		}
	}
	*seq = sequence;

	if (show_time) {
		static struct timeval tv, otv;
		int deltamicro;

		gettimeofday(&tv, NULL);
		if (otv.tv_sec) {
			deltamicro = (tv.tv_sec - otv.tv_sec) * 1000 * 1000
				+ tv.tv_usec - otv.tv_usec;
			printf("+ %i.%06i:", deltamicro / 1000 / 1000,
			       deltamicro % (1000*1000));
		} else {
			printf("%03li.%06li:", tv.tv_sec % 1000, tv.tv_usec);
		}
		otv = tv;
	} else {
		/* time works with one file only, avoid the fname */
		printf("%s:", name);
	}
	if (modemask & MODE_HEX) {
		printf(" %08x %08x %08x %08x %08x\n",
		       a[FD_ATTR_TDC_UTC_H],
		       a[FD_ATTR_TDC_UTC_L],
		       a[FD_ATTR_TDC_COARSE],
		       a[FD_ATTR_TDC_FRAC],
		       a[FD_ATTR_TDC_SEQ]);
	}
	if (modemask & MODE_FLOAT) {
		t2 = a[FD_ATTR_TDC_UTC_L] + a[FD_ATTR_TDC_COARSE]
			* .000000008; /* 8ns */
		if (*t1) {
			printf(" %17.9Lf (delta %13.9Lf)\n",
			       t2, t2 - *t1);
		} else {
			printf(" %17.9Lf\n", t2);
		}
		*t1 = t2;
	}
	if (modemask & MODE_PICO) {
		p2 = a[FD_ATTR_TDC_COARSE] * 8000LL
			+ a[FD_ATTR_TDC_FRAC] * 8000LL / 4096;
		delta = p2 - *p1;
		if (delta < 0)
			delta += 1000LL * 1000 * 1000 * 1000;
		if (*p1) {
			printf(" %012lli - delta %012lli", p2, delta);
			if (expect) {
				guess += expect;
				if (guess > 1000LL * 1000 * 1000 * 1000)
					guess -= 1000LL * 1000 * 1000 * 1000;
				printf(" - error %6i", (int)(p2 - guess));
			}
			putchar('\n');
		}
		else {
			printf(" %012lli\n", p2);
			if (expect)
				guess = p2;
		}
		*p1 = p2;
	}
}

#define MAXFD 16
struct zio_control ctrl;

int main(int argc, char **argv)
{
	glob_t glob_buf;
	int i, j, maxfd = 0;
	int fd[MAXFD], seq[MAXFD];
	fd_set allset, curset;
	int modemask = MODE_HEX;
	long double t1[MAXFD] = {0.0,};
	uint64_t p1[MAXFD] = {0LL,};
	uint32_t *attrs;

	if (getenv("FD_EXPECTED_RATE"))
		expect = atoi(getenv("FD_EXPECTED_RATE"));
	if (getenv("FD_SHOW_TIME"))
		show_time = 1;

	while ((i = getopt(argc, argv, "fprh")) != -1) {

		switch(i) {
		case 'f':
			modemask &= ~MODE_HEX;
			modemask |= MODE_FLOAT;
			break;
		case 'p':
			modemask &= ~MODE_HEX;
			modemask |= MODE_PICO;
			break;
		case 'r':
		case 'h':
			modemask |= MODE_HEX;
			break;
		}
	}
	/* adjust for consumed arguments */
	argv[optind - 1] = argv[0];
	argc -= (optind - 1);
	argv += (optind - 1);

	if (argc < 2) {
		/* rebuild argv using globbing */
		glob_buf.gl_offs = 1;
		/* "????" spits a trigraph warning: use "*" instead (bah!) */
		glob("/dev/fd-*-0-0-ctrl", GLOB_DOOFFS, NULL, &glob_buf);
		glob("/dev/zio/fd-*-0-0-ctrl",
		     GLOB_DOOFFS | GLOB_APPEND, NULL, &glob_buf);
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

	if (argc >= MAXFD) {
		fprintf(stderr, "%s: too many file names\n", argv[0]);
		exit(1);
	};

	FD_ZERO(&allset);
	for (i = 1; i < argc; i++) {
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
	while (1) {
		curset = allset;
		switch(select(maxfd + 1, &curset, NULL, NULL, NULL)) {
		case -1:
			if (errno == EINTR)
				continue;
			fprintf(stderr, "%s: select: %s\n", argv[0],
				strerror(errno));
			exit(1);
		case 0:
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
			event(attrs, argv[i], seq + i, modemask,
			      t1 + i, p1 + i);
		}
	}
	return 0;
}
