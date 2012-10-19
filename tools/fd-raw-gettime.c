/*
 * a tool to get the fdelay time from sysfs
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
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <linux/zio.h>
#include <linux/zio-user.h>
#include <fine-delay.h>
#include "fdelay-raw.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

struct time_word {
	uint32_t val;
	char *name;
};

struct time_word words[] = {
	{0, "utc-h"}, /* This must be first, as this forces hardware access */
	{0, "utc-l"},
	{0, "coarse"},
};

int main(int argc, char **argv)
{
	char *sysnames[10];
	char path[80];
	int i, err;

	i = fdelay_get_sysnames(sysnames);
	if (!i) {
		fprintf(stderr, "%s: no fine-delay devices\n", argv[0]);
		exit(1);
	}
	if (i > 1) {
		fprintf(stderr, "%s: several fine-delay devices, using %s\n",
			argv[0], sysnames[0]);
	}

	for (i = 0, err = 0; i < ARRAY_SIZE(words); i++) {
		sprintf(path, "%s/%s", sysnames[0], words[i].name);
		if (fdelay_sysfs_get(path, &words[i].val) != 0)
			err++;
	}
	if (err) {
		fprintf(stderr, "%s: got %i errors reading %i attributes\n",
			argv[0], err, ARRAY_SIZE(words));
	}
	printf("%i.%09li\n", words[1].val, (long)words[2].val * 8);
	return 0;
}
