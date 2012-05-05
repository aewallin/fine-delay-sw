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
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <linux/zio.h>
#include <linux/zio-user.h>
#include <fine-delay.h>

enum {MODE_HEX, MODE_FLOAT, MODE_PICO};

int expect;

void event(uint32_t *a, char *name, int *seq, int mode, long double *t1,
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

	printf("%s: ", name);
	switch(mode) {
	case MODE_HEX:
		printf("%08x %08x %08x %08x %08x\n",
		       a[FD_ATTR_TDC_UTC_H],
		       a[FD_ATTR_TDC_UTC_L],
		       a[FD_ATTR_TDC_COARSE],
		       a[FD_ATTR_TDC_FRAC],
		       a[FD_ATTR_TDC_SEQ]);
		break;

	case MODE_FLOAT:
		t2 = a[FD_ATTR_TDC_UTC_L] + a[FD_ATTR_TDC_COARSE]
			* .000000008; /* 8ns */
		if (*t1) {
			printf("%17.9Lf (delta %13.9Lf)\n",
			       t2, t2 - *t1);
		} else {
			printf("%17.9Lf\n", t2);
		}
		*t1 = t2;
		break;

	case MODE_PICO:
		p2 = a[FD_ATTR_TDC_COARSE] * 8000LL
			+ a[FD_ATTR_TDC_FRAC] * 4096LL / 8000;
		delta = p2 - *p1;
		if (delta < 0)
			delta += 1000LL * 1000 * 1000 * 1000;
		if (*p1) {
			printf("%012lli - delta %012lli", p2, delta);
			if (expect) {
				guess += expect;
				if (guess > 1000LL * 1000 * 1000 * 1000)
					guess -= 1000LL * 1000 * 1000 * 1000;
				printf(" - error %6i", (int)(p2 - guess));
			}
			putchar('\n');
		}
		else {
			printf("%012lli\n", p2);
			if (expect)
				guess = p2;
		}
		*p1 = p2;
		break;
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
	int mode = MODE_HEX;
	long double t1[MAXFD] = {0.0,};
	uint64_t p1[MAXFD] = {0LL,};
	uint32_t *attrs;

	if (getenv("EXPECTED_RATE"))
		expect = atoi(getenv("EXPECTED_RATE"));

	if (argc > 1 && !strcmp(argv[1], "-f")) {
		mode = MODE_FLOAT;
		argv[1] = argv[0];
		argv++, argc--;
	}
	if (argc > 1 && !strcmp(argv[1], "-p")) {
		mode = MODE_PICO;
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
			event(attrs, argv[i], seq + i, mode, t1 + i, p1 + i);
		}
	}
	return 0;
}
