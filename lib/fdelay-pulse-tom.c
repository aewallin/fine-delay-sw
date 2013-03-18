#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <ctype.h>

#include "fdelay-lib.h"

static void help(int argc, char *argv[])
{
	fprintf(stderr, "%s: Use \"%s [options]\n", argv[0], argv[0]);
	fprintf(stderr, "\t Options are: \n");
	fprintf(stderr, "\t  -d device  : specify device index of hex busid (default: first card available)\n");
	fprintf(stderr, "\t  -m mode    : the mode: disable, pulse or delay (mandatory)\n");
	fprintf(stderr, "\t  -o channel : the output channel, 1..4 (mandatory) \n");
	fprintf(stderr, "\t  -a time    : specify trigger absolute time (default: none)\n");
	fprintf(stderr, "\t  -s time    : specify trigger relative time (default: 1s in pulse generator mode and 0.5 us in delay mode).\n");
	fprintf(stderr, "\t  -w time    : specify output pulse width (default: 200 ns). \n");
	fprintf(stderr, "\t  -g time    : specify output pulse spacing. \n");
	fprintf(stderr, "\t  -t         : wait for trigger before exiting (default: on) \n");
	fprintf(stderr, "\t  -q count   : number of pulses to generate (default: 1)\n");
	fprintf(stderr, "\t  -c         : generate infinite number of pulses (default: off)\n");
	fprintf(stderr, "Time format is a sum of times expressed in (full, milli, micro, nano, pico)seconds, for example:\n");
	fprintf(stderr, "1s+20u-100n is 1 sec, 19.9 us. Fractional numbers are NOT allowed.\n\n");

}

static struct fdelay_time ts_add(struct fdelay_time a, struct fdelay_time b)
{                                                                                                                                                            
	a.frac += b.frac;
	if(a.frac >= 4096)
	{
		a.frac -= 4096;
		a.coarse++;
        }                                                                                                                                                    
        a.coarse += b.coarse;
        if(a.coarse >= 125000000)
	{
                a.coarse -= 125000000;
                a.utc ++;
        }
	a.utc += b.utc;
        return a; }        

static void parse_time(char *s, struct fdelay_time *t)
{
    int64_t time_ps = 0;
    int64_t extra_seconds = 0;
    int64_t sign = 1;
    int64_t term = 0;
    int64_t scale = 1;

    const int64_t one_second = 1000000000000LL;

    char c, *buf = s;
    
    while((c = *buf++) != 0)
    {
	switch(c)
	{
	    case '+':
		if(scale == one_second)
		    extra_seconds += sign * term;
		else
        	    time_ps += sign * term * scale;

    		term = 0;
    		sign = 1;
    		break;
	    case '-':
		if(scale == one_second)
		    extra_seconds += sign * term;
		else
        	    time_ps += sign * term * scale;

    		term = 0;
    		sign = -1;
    		break;

    	    case 's':
		scale =  one_second;
		break;
    	    case 'm':
    		scale = 1000000000LL;
    		break;
    	    case 'u':
    		scale = 1000000LL;
    		break;
    	    case 'n':
    		scale = 1000LL;
    		break;
    	    case 'p':
    		scale = 1LL;
    		break;
    	    default:
    		if(isdigit(c))
    		{
		    term *= 10LL;
    		    term += (int64_t) (c - '0');
    		    break;
    		} else {
    		    fprintf(stderr, "Error while parsing time string '%s'\n", s);
    		    exit(-1);
    		}
    	}
    }

    if(scale == one_second)
	extra_seconds += sign * term;
    else
        time_ps += sign * term * scale;


    while(time_ps < 0)
    {
	time_ps += one_second;
	extra_seconds--;
    }

    fdelay_pico_to_time((uint64_t *) &time_ps, t);
    
    t->utc += extra_seconds;

//    printf("dbg: raw %lld, %lld, converted: %lld s %d ns %d ps\n", extra_seconds,time_ps, t->utc, t->coarse * 8, t->frac * 8000 / 4096);
}

void dump_ts(char *title, struct fdelay_time t)
{
    printf("%s: secs %lld coarse %d frac %d\n", title, t.utc, t.coarse, t.frac);
}

int main(int argc, char **argv)
{
    struct fdelay_time t_start, t_delta, t_width, t_current;
    int mode = -1;
    int channel = -1;
    int count = 1;
    int wait_trigger = 0;
    int opt;
    int relative = 1;
    int devid = 0;


    int64_t default_width = 250000;    fdelay_pico_to_time(&default_width, &t_width);

    while ((opt = getopt(argc, argv, "hctd:m:o:a:s:w:g:q:")) != -1) {
	switch(opt)
	{
	    case 'h':
		help(argc, argv);
		break;
		
	    case 'c': 
		count = -1;
		break;
	    case 'q':
		count = atoi(optarg);
		break;
	    case 't':
		wait_trigger = 1;
		break;
	    case 'g':
		parse_time(optarg, &t_delta);
		break;
	    case 'w':
		parse_time(optarg, &t_width);
		break;
	    case 's':
		parse_time(optarg, &t_start);
		relative = 1;
		break;
	    case 'a':
		parse_time(optarg, &t_start);
		relative = 0;
		break;
	    case 'o':
		channel = atoi(optarg);
		if(channel < 1 || channel > 4)
		{
		    fprintf(stderr, "Invalid output channel.\n");
		    exit(1);
		}
		break;

	    case 'm':
		if(!strcmp(optarg, "pulse"))
		    mode = FD_OUT_MODE_PULSE;
		else if(!strcmp(optarg, "delay"))
		    mode = FD_OUT_MODE_DELAY;
		else if(!strcmp(optarg, "disable"))
		    mode = FD_OUT_MODE_DISABLED;
		else {
		    fprintf(stderr, "Invalid output mode.\n");
		    exit(1);
		}
		break;
	    
	    case 'd':
		sscanf(optarg, "%i", &devid);
		break;
	}
    
    }
      
    struct fdelay_board *b;
    struct fdelay_pulse p;
    /* init before going on parsing */
    int i = fdelay_init();
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
    
    if(mode <0 || channel < 0)
    {
	fprintf(stderr,"You must specify the mode and the channel to generate pulses\n");
    }

    if(mode == FD_OUT_MODE_PULSE && relative)
    {
	fdelay_get_time(b, &p.start);
	p.start = ts_add(p.start, t_start);
    } else {
	p.start = t_start;
    }    

    p.end = ts_add(p.start, t_width);
    p.loop = t_delta;
    p.rep = count;
    p.mode = mode;
    
    dump_ts("Start", p.start);
    dump_ts("End", p.end);
    dump_ts("Delta", p.loop);
    

    printf("mode %d channel %d count %d\n", mode, channel, count);

    /* And finally work */
    if (fdelay_config_pulse(b, channel - 1, &p) < 0) {
    	fprintf(stderr, "%s: fdelay_config_pulse(): %s\n",
		argv[0], strerror(errno));
	exit(1);
    }

    while (wait_trigger) {
    	usleep(10 * 1000);
    	i = fdelay_has_triggered(b, channel - 1);
	if (i < 0) {
		fprintf(stderr, "%s: waiting for trigger: %s\n",
			argv[0], strerror(errno));
		exit(1);
	}
	wait_trigger = !i;
    }

	fdelay_close(b);
	fdelay_exit();
	return 0;
}
