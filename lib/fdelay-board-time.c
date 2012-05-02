/* Simple demo that acts on the time of the first board */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "fdelay.h"

int main(int argc, char **argv)
{
	struct fdelay_board *b;
	int i, get = 0, host = 0;
	struct fdelay_time t;

	if (argc != 2) {
		fprintf(stderr, "%s: Use \"%s \"get\"|\"host\"|"
			"<float-value>\"\n", argv[0], argv[0]);
		exit(1);
	}
	/* Crappy parser */
	if (!strcmp(argv[1], "get"))
		get = 1;
	else if (!strcmp(argv[1], "host"))
		host = 1;
	else {
		double nano;
		long long sec;

		memset(&t, 0, sizeof(t));
		i = sscanf(argv[1], "%lli%lf\n", &sec, &nano);
		if (i < 1) {
			fprintf(stderr, "%s: Not a number \"%s\"\n",
				argv[0], argv[1]);
			exit(1);
		}
		t.utc = sec;
		t.coarse = nano * 1000 * 1000 * 1000 / 8;
	}

	i = fdelay_init();
	if (i < 0) {
		fprintf(stderr, "%s: fdelay_init(): %s\n", argv[0],
			strerror(errno));
		exit(1);
	}
	if (i == 0) {
		fprintf(stderr, "%s: no boards found\n", argv[0]);
		exit(1);
	}
	if (i != 1) {
		fprintf(stderr, "%s: found %i boards, using first one\n",
			argv[0], i);
	}

	b = fdelay_open(0, -1);
	if (!b) {
		fprintf(stderr, "%s: fdelay_open(): %s\n", argv[0],
			strerror(errno));
		exit(1);
	}

	if (get) {
		if (fdelay_get_time(b, &t) < 0) {
			fprintf(stderr, "%s: fdelay_get_time(): %s\n", argv[0],
				strerror(errno));
			exit(1);
		}
		printf("%lli.%09li\n", (long long)t.utc, (long)t.coarse * 8);
		fdelay_close(b);
		fdelay_exit();
		return 0;
	}

	if (host) {
		if (fdelay_set_host_time(b) < 0) {
			fprintf(stderr, "%s: fdelay_set_host_time(): %s\n",
				argv[0], strerror(errno));
			exit(1);
		}
		fdelay_close(b);
		fdelay_exit();
		return 0;
	}

	if (fdelay_set_time(b, &t) < 0) {
		fprintf(stderr, "%s: fdelay_set_host_time(): %s\n",
			argv[0], strerror(errno));
		exit(1);
	}
	fdelay_close(b);
	fdelay_exit();
	return 0;
}
