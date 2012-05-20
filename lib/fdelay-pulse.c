/* Simple demo that acts on the time of the first board */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "fdelay-lib.h"

static int exiterr(int argc, char **argv)
{
	fprintf(stderr, "%s: Use \"%s [<dev>] <mode> <ch> <rep> "
		"<t1> <t2> <t3>\"\n", argv[0], argv[0]);
	fprintf(stderr, "\t <dev> is index ([0..]) or hex busid\n");
	fprintf(stderr, "\t <mode> is \"disable\",\"pulse\",\"delay\"\n");
	fprintf(stderr, "\t <t> is <utc>.<us>[+<ps>]\"\n");
	fprintf(stderr, "\t Note that <t2> and <t3> are relative to <t1>\n");
	fprintf(stderr, "\t (if <utc> uses a leading + it's added to now)\n");
	exit(1);
}

static int parse_time(struct fdelay_board *b, char *s, struct fdelay_time *t)
{
	unsigned long u, m = 0, p = 0;
	char smicro[32];
	int i, relative = 0;

	/*
	 * Hairy: if we scan "%ld%lf", the 0.009999 will become 9998 micro.
	 * Thus, scan as integer and string, so we can count leading zeros
	 */
	if (s[0] == '+') {
		relative = 1;
		s++;
	}
	if (sscanf(s, "%ld.%ld+%ld", &u, &m, &p) < 1) {
		return -1;
	}
	if (m) { /* micro is not zero, check how long it is and scale*/
		sscanf(s, "%ld.%[0-9]", &u, smicro);
		i = strlen(smicro);
		if (i > 6)
			return -1;
		while (i < 6) {
			m *= 10;
			i++;
		}
	}
	t->utc = 0;
	if (relative)
		if (fdelay_get_time(b, t))
			return -1;
	t->utc += u;
	t->coarse = m * 1000 / 8 + p / 8000;
	t->frac = ((p % 8000) << 12) / 8000;
	return 0;
}

int main(int argc, char **argv)
{
	struct fdelay_board *b;
	int i, devid, channel, needwait = 0;
	struct fdelay_pulse p;
	char *rest;

	if (argc < 7)
		exiterr(argc, argv);

	/* Optional argv[1] is "-w" */
	if (!strcmp(argv[1], "-w")) {
		needwait++;
		argv[1] = argv[0];
		argv++;
		argc--;
	}

	/* Next optional argv[1] is a number */
	rest = strdup(argv[1]);
	if (sscanf(argv[1], "%x%s", &devid, rest) != 1) {
		devid = 0;
	} else {
		argv[1] = argv[0];
		argv++;
		argc--;
	}
	free(rest);

	if (argc < 7) /* again: we ate some arguments */
		exiterr(argc, argv);

	/* Crappy parser */
	if (!strcmp(argv[1], "disable"))
		p.mode = FD_OUT_MODE_DISABLED;
	else if (!strcmp(argv[1], "pulse"))
		p.mode = FD_OUT_MODE_PULSE;
	else if (!strcmp(argv[1], "delay"))
		p.mode = FD_OUT_MODE_DELAY;
	else {
		fprintf(stderr, "%s: \"%s\": invalid\n",
			argv[0], argv[1]);
		exiterr(argc, argv);
	}

	rest = strdup(argv[2]);
	if (sscanf(argv[2], "%i%s", &channel, rest) != 1) {
		fprintf(stderr, "%s: channel \"%s\": not a number\n",
			argv[0], argv[2]);
		exiterr(argc, argv);
	}
	free(rest);

	rest = strdup(argv[3]);
	if (sscanf(argv[3], "%i%s", &p.rep, rest) != 1) {
		fprintf(stderr, "%s: rep \"%s\": not a number\n",
			argv[0], argv[2]);
		exiterr(argc, argv);
	}
	free(rest);

	/* init before going on parsing */
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
	b = fdelay_open(devid, -1);
	if (!b)
		b = fdelay_open(-1, devid);
	if (!b) {
		fprintf(stderr, "%s: fdelay_open(0x%x): %s\n", argv[0], devid,
			strerror(errno));
		exit(1);
	}

	/* now we can ask current time: continue parsing */
	if (parse_time(b, argv[4], &p.start)) {
		fprintf(stderr, "%s: can't parse \"%s\"\n", argv[0], argv[4]);
		exiterr(argc, argv);
	}
	if (parse_time(b, argv[5], &p.end)) {
		fprintf(stderr, "%s: can't parse \"%s\"\n", argv[0], argv[5]);
		exiterr(argc, argv);
	}
	if (parse_time(b, argv[6], &p.loop)) {
		fprintf(stderr, "%s: can't parse \"%s\"\n", argv[0], argv[6]);
		exiterr(argc, argv);
	}
	/* end is specified as relative but used as absolute */
	p.end.frac += p.start.frac;
	if (p.end.frac > 4096) {
		p.end.frac -= 4096;
		p.end.coarse++;
	}
	p.end.coarse += p.start.coarse;
	if (p.end.coarse > 125 * 1000 * 1000) {
		p.end.coarse -= 125 * 1000 * 1000;
		p.end.utc++;
	}
	p.end.utc += p.start.utc;

	/* And finally work */
	if (fdelay_config_pulse(b, channel, &p) < 0) {
		fprintf(stderr, "%s: fdelay_cofig_pulse(): %s\n",
			argv[0], strerror(errno));
		exit(1);
	}

	while (needwait) {
		usleep(10 * 1000);
		i = fdelay_has_triggered(b, channel);
		if (i < 0) {
			fprintf(stderr, "%s: waiting for trigger: %s\n",
				argv[0], strerror(errno));
			exit(1);
		}
		needwait = !i;
	}

	fdelay_close(b);
	fdelay_exit();
	return 0;
}
