/* Simple demo that reads samples using the read call */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>

#define FDELAY_INTERNAL // for sysfs_get/set
#include "fdelay-lib.h"

#define PACKED __attribute__((packed))

#define TYPE_START_LOGGING 1
#define TYPE_END_LOGGING 2
#define TYPE_TIMESTAMP 3

PACKED struct binary_timestamp {
	int32_t card_id;
	uint8_t type;
	PACKED struct fdelay_time ts;
};


#define MAX_BOARDS 64

struct board_def {
	struct fdelay_board *b;
	int term_on;
	int64_t input_offset;
	int64_t output_offset;
	int hw_index;
	int in_use;
	
	struct {
		int64_t offset_pps, width, period;
		int enabled;
	} outs [4];
};


struct board_def boards[MAX_BOARDS];

static FILE *log_file = NULL;

void log_write(struct fdelay_time *t, int card_id)
{
	struct binary_timestamp bt;
	if(!log_file)
		return;
	bt.ts = *t;
	bt.type = TYPE_TIMESTAMP;
	bt.card_id = card_id;
	fwrite(&bt, sizeof(struct binary_timestamp), 1, log_file);
	fflush(log_file);
}

void log_start(char *log_file_name)
{
		struct binary_timestamp bt;
		log_file = fopen(log_file_name, "a+");

		if(!log_file)
		{
			fprintf(stderr, "Can't open the log file: %s\n", log_file_name);
			exit(-1);
		}

		bt.card_id = 0;
		bt.type = TYPE_START_LOGGING;
		fwrite(&bt, sizeof(struct binary_timestamp), 1, log_file);
		fflush(log_file);
}

void log_stop()
{
		struct binary_timestamp bt;
		if(!log_file)
			return;
		bt.card_id = 0;
		bt.type = TYPE_END_LOGGING;
		fwrite(&bt, sizeof(struct binary_timestamp), 1, log_file);
		fflush(log_file);
}

int64_t parse_num(const char *n_str)
{
	struct {
		char unit;
		int64_t multiplier;
	} units[] = {
		{'p', 1LL},
		{'n', 1000LL},
		{'u', 1000000LL},
		{'m', 1000000000LL},
		{'s', 1000000000000LL},
		{' ', 0}
	};
	
	int64_t n;
	char unit;
	
 	int rv = sscanf(n_str,"%lli%c", &n, &unit);
 
 	if(rv == 1)
 		return n;
 	else if (rv == 2) 
 	{
 		int i;
 		for(i=0; units[i].multiplier; i++)
 			if(units[i].unit == unit)
 				return units[i].multiplier * n;
 		
 		fprintf(stderr,"Unrecognized numeric constant '%s' (wrong units?)\n", n_str);
 		exit(-1);
 	}	

	fprintf(stderr,"Unrecognized numeric constant '%s'\n", n_str);
 	exit(-1);
}

#define MAX_TOKENS 16
#define MAX_TOK_LENGTH 256

typedef char token_array[MAX_TOKENS][MAX_TOK_LENGTH];

/* returns: tokenized arguments to tokens, command (1st word) to cmd */
int next_command(FILE *f_config, char *cmd, token_array tokens)
{
	char line [1024];
	char *running;
	const char *delims=" \n\r\t";
	int i;
	int n = 0;
	
	do {
		if(feof(f_config))
			return -1;
		
		fgets(line, sizeof(line), f_config);
	
		running = strdupa(line);
		while(*running == ' ' || *running == '\t') running++;
	
		strncpy(cmd, strsep (&running , delims), MAX_TOK_LENGTH);

	} while(cmd[0] == '#' || cmd[0] == ' ' || cmd[0] == '\n' || cmd[0] == '\r' || !cmd[0]);

	for(i=0;i<MAX_TOKENS;i++)
	{
		char *token = strsep (&running , delims);
		
		if(token == NULL)
			return n;

		if(strlen(token) > 0)
		{
//			printf("tok %p\n", tokens[0]);
			strncpy(&tokens[n][0], token, MAX_TOK_LENGTH);
			n++;
		}
	}
	
	return 0;
}

#define CUR boards[current_board]

void load_config(const char *config_file)
{
	token_array args;
	char cmd [MAX_TOK_LENGTH];
	int current_board = 0;
	int n_args;
	

	FILE *f_config=fopen(config_file, "r");
	
	if(!f_config)
	{
		fprintf(stderr,"Can't open configuration file '%s'\n", config_file);
		exit(-1);
	}

	memset(boards, 0, sizeof(boards));
	
	while((n_args = next_command(f_config, cmd, args)) >= 0)
	{
		if(!strcmp(cmd, "board"))
		{
			current_board = parse_num(args[0]);
			CUR.in_use = 1;
		}	
		


		if(!strcmp(cmd, "hw_index"))
		{
			CUR.hw_index = parse_num(args[0]);
			printf("Adding board %d, hw_index %d\n", current_board, CUR.hw_index);
		
		}
			
		if(!strcmp(cmd, "termination"))
			CUR.term_on = parse_num(args[0]);

		if(!strcmp(cmd, "input_offset"))
			CUR.input_offset = parse_num(args[0]);

		if(!strcmp(cmd, "output_offset"))
			CUR.output_offset = parse_num(args[0]);

		if(!strcmp(cmd, "out"))
		{
			int index = parse_num(args[0]) - 1;
			
			if(index < 0 || index > 3)
			{
				fprintf(stderr,"Invalid output index\n");
				exit(-1);
			}

			CUR.outs[index].offset_pps = parse_num(args[1]);
			CUR.outs[index].width = parse_num(args[2]);
			CUR.outs[index].period = parse_num(args[3]);
			CUR.outs[index].enabled = 1;
			
//			printf("OutCfg: %d %lli %lli %lli\n", index, CUR.outs[index].offset_pps, CUR.outs[index].width, CUR.outs[index].period);
		}	
		
		if(!strcmp(cmd, "log_file"))
			log_start(args[0]);
	
	}
	
	fclose(f_config);
}

#undef CUR

void enable_termination(struct fdelay_board *b, int enable)
{
    int i;

    int hwval = fdelay_get_config_tdc(b);

    if(enable)
    	hwval |= FD_TDCF_TERM_50;
    else
	hwval &= ~FD_TDCF_TERM_50;
	
    for(i=0;i<3;i++)
    {
	fdelay_set_config_tdc(b, hwval);
	hwval ^=  FD_TDCF_TERM_50;
	usleep(10000);
    }
}

void enable_wr(struct fdelay_board *b, int index)
{
	int lock_retries = 10;
	printf("Locking to WR network [board=%d]...", index);
	fflush(stdout);
	fdelay_wr_mode(b, 0);
	sleep(2);
	fdelay_wr_mode(b, 1);

	while(fdelay_check_wr_mode(b))
	{
			printf(".");
	    fflush(stdout);
	    sleep(1);
	    if(lock_retries-- == 0)
	    {
				fprintf(stderr," WR lock timed out\n");
				exit(1);
	    }
	}

	printf("\n");
	fflush(stdout);
}

/* Add two timestamps */
static struct fdelay_time ts_add(struct fdelay_time a, struct fdelay_time b)
{
    a.frac += b.frac;
    if(a.frac >= 4096)
    {
        a.frac -= 4096;
	a.coarse++;
    }
    a.coarse += b.coarse;
    if(b.coarse >= 125000000)
    {
        a.coarse -= 125000000;
	a.utc ++;
    }
    a.utc += b.utc;
    return a;
}
                                                                    

int configure_board(struct board_def *bdef)
{
	struct fdelay_board *b;
	int i;
	
	b = fdelay_open(-1, bdef->hw_index);
	
	if(!b)
	{
		fprintf(stderr,"Can't open fdelay board @ hw_index %d\n", bdef->hw_index);
		exit(-1);
	}
	
	bdef->b = b;
	
	enable_termination(b, bdef->term_on);
	enable_wr(b, bdef->hw_index);

	int val = bdef->input_offset;
	fdelay_sysfs_set((struct __fdelay_board *)b, "fd-input/user-offset", (uint32_t *)&val);
	
	for(i=0;i<4;i++)
	{
	    char path[1024];
    	    int val = bdef->output_offset;

	    snprintf(path, sizeof(path), "fd-ch%d/user-offset", i+1);
	    fdelay_sysfs_set((struct __fdelay_board *)b, path,(uint32_t *) &val);
	}

	for(i=0;i<4;i++)
	{
	    if(bdef->outs[i].enabled)
	    {
		struct fdelay_time t_cur, pps_offset, width;
		struct fdelay_pulse p;
		
		printf("Configure output %d\n", i+1);
		fdelay_get_time(bdef->b, &t_cur);
		fdelay_pico_to_time(&bdef->outs[i].offset_pps, &pps_offset);
		fdelay_pico_to_time(&bdef->outs[i].width, &width);

		
		
		t_cur.utc += 2;
		t_cur.coarse = 0;
		t_cur.frac = 0;
		t_cur = ts_add(pps_offset, t_cur);
		
		p.rep = -1;
		p.mode = FD_OUT_MODE_PULSE;
		p.start = t_cur;
		p.end = ts_add(t_cur, width);
		fdelay_pico_to_time(&bdef->outs[i].period, &p.loop);
		fdelay_config_pulse(bdef->b, i, &p);
	    }
	}
    return 0;
}

void handle_readout(struct board_def *bdef)
{
    int64_t t_ps;
    struct fdelay_time t;

    while(fdelay_read(bdef->b, &t, 1, O_NONBLOCK) == 1)
    {	    
	t_ps = (t.coarse * 8000LL) + ((t.frac * 8000LL) >> 12);
	printf("seq %5i: time %lli s, %lli.%03lli ns [%x]\n",   t.seq_id, t.utc, t_ps / 1000LL, t_ps % 1000LL, t.coarse);
	log_write(&t, bdef->hw_index);
    }
}


void sighandler(int sig)
{
    if(sig == SIGINT || sig== SIGTERM || sig==SIGKILL)
    {
	fprintf(stderr,"Cleaniung up...\n");
	log_stop();
	exit(0);
    }
}

int main(int argc, char *argv[])
{
	int i;
	
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGKILL, sighandler);
	
	if(argc < 2)
	{
		printf("usage: %s <configuration-file>\n", argv[0]);
		return 0;
	}
	
	load_config(argv[1]);
	
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

	for(i=0;i<MAX_BOARDS;i++)
		if(boards[i].in_use)
			configure_board(&boards[i]);

	for(;;)
	{
	    for(i=0;i<MAX_BOARDS;i++)
		if(boards[i].in_use)
		    handle_readout(&boards[i]);
	    usleep(10000);
	}
}

