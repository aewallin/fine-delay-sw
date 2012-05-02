/* Simple demo that acts on the termination of the first board */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fine-delay.h>
#include "fdelay.h"

int main(int argc, char **argv)
{
	struct fdelay_board *b;
	int i, hwval, newval;

	if (argc > 2) {
		fprintf(stderr, "%s: Use \"%s 1|0\n", argv[0], argv[0]);
		exit(1);
	}
	newval = -1;
	if (argc > 1) {
		if (!strcmp(argv[1], "0"))
			newval = 0;
		else if (!strcmp(argv[1], "1"))
			newval = 1;
		else {
			fprintf(stderr, "%s: arg \"%s\" is not 0 nor 1\n",
				argv[0], argv[1]);
			exit(1);
		}
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

	hwval = fdelay_get_config_tdc(b);
	switch(newval) {
	case 1:
		hwval |= FD_TDCF_TERM_50;
		break;
	case 0:
		hwval &= ~FD_TDCF_TERM_50;
		break;
	}
	fdelay_set_config_tdc(b, hwval);
	hwval = fdelay_get_config_tdc(b);
	printf("%s: termination is %s\n", argv[0],
	       hwval & FD_TDCF_TERM_50 ? "on" : "off");

	fdelay_close(b);
	fdelay_exit();
	return 0;
}
