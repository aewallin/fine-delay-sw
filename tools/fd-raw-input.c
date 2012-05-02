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

#define MAXFD 16
struct zio_control ctrl;

int main(int argc, char **argv)
{
	glob_t glob_buf;
	int fd[MAXFD], seq[MAXFD];
	int i, j, maxfd = 0, sequence, last = 0;
	fd_set allset, curset;
	int floatmode = 0;
	long double t1 = 0.0, t2;
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
			sequence = attrs[FD_ATTR_TDC_SEQ];
			if (seq[i] != -1) {
				if (sequence  - seq[i] != 1)
					printf("%s: LOST %i events\n", argv[i],
					       sequence - seq[i] - 1);
			}
			seq[i] = sequence;
			printf("%s: ", argv[i]);
			if (floatmode) {
				t2 = attrs[FD_ATTR_TDC_UTC_L] +
					attrs[FD_ATTR_TDC_COARSE]
					* .000000008; /* 8ns */
				if (t1) {
					printf("%17.9Lf (delta %13.9Lf)\n",
					       t2, t2 - t1);
				} else {
					printf("%17.9Lf\n", t2);
				}
				t1 = t2;
				continue;
			}
			printf("%08x %08x %08x %08x %08x\n",
			       attrs[FD_ATTR_TDC_UTC_H],
			       attrs[FD_ATTR_TDC_UTC_L],
			       attrs[FD_ATTR_TDC_COARSE],
			       attrs[FD_ATTR_TDC_FRAC],
			       attrs[FD_ATTR_TDC_SEQ]);
		}
	}
	return 0;
}
