/* Simple demo that acts on the time of the first board */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "fdelay-lib.h"

int main(int argc, char **argv)
{
	struct fdelay_board *b;
	int i, get = 0, host = 0, wr_on = 0, wr_off = 0;
	struct fdelay_time t;
	int dev = 0;

	/* Parse, and kill "-i <devindex>" */
	if (argc > 2 && !strcmp(argv[1], "-i")) {
		dev = strtol(argv[2], NULL, 0);
		argv[2] = argv[0];
		argc -= 2;
	}

	if (argc != 2) {
		fprintf(stderr,
			"%s: Use \"%s [-i <devindex> \"get\"|\"host\"|\"local\"|\"wr\"|"
			"<float-value>\"\n", argv[0], argv[0]);
		exit(1);
	}

	/* Crappy parser */
	if (!strcmp(argv[1], "get"))
		get = 1;
	else if (!strcmp(argv[1], "host"))
		host = 1;
	else if (!strcmp(argv[1], "wr"))
		wr_on = 1;
	else if (!strcmp(argv[1], "local"))
		wr_off = 1;
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
	if (i != 1)
		fprintf(stderr, "%s: found %i boards\n", argv[0], i);

	b = fdelay_open(dev, -1);
	if (!b) {
		fprintf(stderr, "%s: fdelay_open(): %s\n", argv[0],
			strerror(errno));
		exit(1);
	}

	if (i != 1)
		fprintf(stderr, "%s: using board %d\n", argv[0], dev);

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

	if (wr_on) {
		printf("Locking the card to WR: ");

		if (fdelay_wr_mode(b, 1) < 0) {
			fprintf(stderr, "%s: fdelay_wr_mode(): %s\n",
				argv[0], strerror(errno));
			exit(1);
		}

		while (fdelay_check_wr_mode(b) != 0) {
			printf(".");
			fflush(stdout);
			sleep(1);
		}

		printf(" locked!\n");
		fdelay_close(b);
		fdelay_exit();
		return 0;
	}

	if (wr_off) {
		if (fdelay_wr_mode(b, 0) < 0) {
			fprintf(stderr, "%s: fdelay_wr_mode(): %s\n",
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
