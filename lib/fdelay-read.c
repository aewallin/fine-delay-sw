/* Simple demo that reads samples using the read call */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "fdelay-lib.h"

int main(int argc, char **argv)
{
	struct fdelay_board *b;
	int i, j,npulses;
	struct fdelay_time *t;

	if (argc != 2) {
		fprintf(stderr, "%s: Use \"%s <nsamples>\n", argv[0], argv[0]);
		exit(1);
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

	npulses = atoi(argv[1]);
	t = calloc(npulses, sizeof(*t));
	if (!t) {
		fprintf(stderr, "%s: calloc(): %s\n", argv[0],
			strerror(errno));
		exit(1);
	}

	/* Read twice: first blocking then non-blocking */
	fprintf(stderr, "%s: reading %i pulses in blocking mode...",
		argv[0], npulses);
	i = fdelay_read(b, t, npulses, 0);
	fprintf(stderr, " got %i of them\n", i);
	for (j = 0; j < i; j++) {
		printf("seq %5i: time %lli.%09li + %04x\n",
		       t[j].seq_id, t[j].utc, (long)t[j].coarse * 8, t[j].frac);
	}
	if (i < 0) {
		fprintf(stderr, "%s: fdelay_read: %s\n",
			argv[0], strerror(errno));
		exit(1);
	}

	fprintf(stderr, "%s: reading %i pulses in non-blocking mode...",
		argv[0], npulses);
	i = fdelay_read(b, t, npulses, O_NONBLOCK);
	fprintf(stderr, " got %i of them\n", i);
	for (j = 0; j < i; j++) {
		printf("seq %5i: time %lli.%09li + %04x\n",
		       t[j].seq_id, t[j].utc, (long)t[j].coarse * 8, t[j].frac);
	}

	fdelay_close(b);
	fdelay_exit();
	return 0;
}
