/* Silly thing that lists installed fine-delay boards */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define FDELAY_INTERNAL /* hack... */
#include "fdelay-lib.h"

int main(int argc, char **argv)
{
	int i, j;
	struct __fdelay_board *b;
	struct fdelay_board *ub;

	i = fdelay_init();
	if (i < 0) {
		fprintf(stderr, "%s: fdelay_init(): %s\n", argv[0],
			strerror(errno));
		exit(1);
	}
	printf("%s: found %i boards\n", argv[0], i);

	for (j = 0; j < i; j++) {
		ub = fdelay_open(j, -1);
		b = (typeof(b))ub;
		printf("  dev_id %04x, %s, %s\n", b->dev_id, b->devbase,
		       b->sysbase);
	}
	fdelay_exit();
	return 0;
}

