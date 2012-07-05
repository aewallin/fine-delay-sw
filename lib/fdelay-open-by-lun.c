/* Silly thing that lists installed fine-delay boards */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define FDELAY_INTERNAL /* hack... */
#include "fdelay-lib.h"

int main(int argc, char **argv)
{
	int i;
	struct __fdelay_board *b;
	struct fdelay_board *ub;

	int lun;
	char *endptr;

	if (argc != 2)
		goto usage;
	lun = strtol(argv[1], &endptr, 0);
	if (*endptr != 0) 
		goto usage;

	i = fdelay_init();
	if (i < 0) {
		fprintf(stderr, "%s: fdelay_init(): %s\n", argv[0],
			strerror(errno));
		exit(1);
	}
	printf("%s: found %i boards\n", argv[0], i);

	ub = fdelay_open_by_lun(lun);
	if (ub == NULL) {
		fprintf(stderr, "could not open lun %d\n", lun);
		exit(1);
	}
	b = (typeof(b))ub;
	printf("lun:%d  dev_id %04x, %s, %s\n", lun, b->dev_id, b->devbase,
	       b->sysbase);
	fdelay_exit();
	return 0;

usage:
	printf("usage: %s lun\n", argv[0]);
	return -1;
}

