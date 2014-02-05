/* fmc-fine-delay raw_tdc example program
 * AW 2014-02-05
 * based on  /fine-delay-sw/NewLogger/fdelay-gs.c
 * and       /fine-delay-sw/zio/tools/zio-dump.c
 * 
 * as of 2014-02-04, raw_tdc is experimental and works with:
 * $ modinfo zio
 * version:        zio-v1.0-65-g02326c8
 * $ modinfo fmc-fine-delay 
 * version:        fine-delay-sw-v2013-06-24-g5737198
 *
 * load kernel module using raw-mode: 
 * $ sudo modprobe fmc-fine-delay raw_tdc=1 fifo_len=16384
 * 
 * the maximum number of time-stamps per ZIO block is set by
 * echo 1000 > /sys/bus/zio/devices/fd-0200/fd-input/trigger/post-samples
 * 
 * the number of ZIO blocks in the buffer is set by
 * echo 1000 > /sys/bus/zio/devices/fd-0200/fd-input/chan0/buffer/max-buffer-len
 * 
 * */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>

#include "linux/zio-user.h"

#define FDELAY_INTERNAL // for sysfs_get/set
#include "fdelay-lib.h"

//#define MAX_BOARDS 1

// keep information about the board here.
struct board_def {
	struct fdelay_board *b;
	int term_on;
	int64_t input_offset;
	int64_t output_offset;
	int hw_index; // 0x0100 for the first board
	//int in_use;
	int fd_ctrl; // file-descriptor to zio ctrl
	int fd_data; // file-descritor to data
	
	/* not used
	struct {
		int64_t offset_pps, width, period;
		int enabled;
	} outs [4]; */
};

/* Add two timestamps */
struct fdelay_time my_ts_add(struct fdelay_time a, struct fdelay_time b) {
    a.frac += b.frac;
    if(a.frac >= 4096)    {
        a.frac -= 4096;
		a.coarse++;
    }
    a.coarse += b.coarse;
    if(a.coarse >= 125000000) {
        a.coarse -= 125000000;
		a.utc ++;
    }
    a.utc += b.utc;
    return a;
}

struct board_def my_board;

void enable_termination(struct fdelay_board *b, int enable) {
    int i;
    int hwval = fdelay_get_config_tdc(b);

    if(enable)
    	hwval |= FD_TDCF_TERM_50;
    else
	hwval &= ~FD_TDCF_TERM_50;
	
    for(i=0;i<3;i++) {
		fdelay_set_config_tdc(b, hwval);
		hwval ^=  FD_TDCF_TERM_50;
		usleep(10000);
    }
}
                                                                
int configure_board(struct board_def *bdef) {
	struct fdelay_board *b;
	int i;
	
	b = fdelay_open(-1, bdef->hw_index);
	
	if(!b) {
		fprintf(stderr,"Can't open fdelay board @ hw_index %x\n", bdef->hw_index);
		exit(-1);
	}
	
	bdef->b = b;
	
	enable_termination(b, bdef->term_on);
	//enable_wr(b, bdef->hw_index);

	int val = bdef->input_offset;
	fdelay_sysfs_set((struct __fdelay_board *)b, "fd-input/user-offset", (uint32_t *)&val);
	
	for(i=0;i<4;i++)
	{
	    char path[1024];
    	    int val = bdef->output_offset;

	    snprintf(path, sizeof(path), "fd-ch%d/user-offset", i+1);
	    fdelay_sysfs_set((struct __fdelay_board *)b, path,(uint32_t *) &val);
	}

/*
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
		t_cur = my_ts_add(pps_offset, t_cur);
		
		p.rep = -1;
		p.mode = FD_OUT_MODE_PULSE;
		p.start = t_cur;
		p.end = my_ts_add(t_cur, width);
		fdelay_pico_to_time(&bdef->outs[i].period, &p.loop);
		fdelay_config_pulse(bdef->b, i, &p);
	    }
	}*/
    return 0;
}


/*
long long int prev_ps;
long long int start_of_program;
int first = 1;

*/

void coarse_fract_to_picos(struct fd_time *time, uint64_t *pico) {
	uint64_t p; 
	p = time->frac * 8000 / 4096; // fractions of the 8 ns clock.
	p += time->coarse * 8000; // 8ns per coarse tic (125 MHz fpga clock)
  	*pico = p;
}

/* 
 * reminder to self: timestamp looks like this (fdelay-lib.h)
   struct fdelay_time {
		uint64_t utc;
		uint32_t coarse;
		uint32_t frac;
		uint32_t seq_id;
		uint32_t channel;
    };
 
 each raw sample is in fd_time format (fine-delay.h)
 * struct fd_time {
	uint64_t utc;      // 8 bytes
	uint32_t coarse;   // 4
	uint32_t frac;     // 4
	uint32_t channel;  // 4
	uint32_t seq_id;   // 4
};
 * */

unsigned char buf[1024*1024]; // large buffer
int next=0; // keep track of missing samples
uint64_t previous_utc=0; // keep track of seconds
uint64_t nstamps;
uint64_t nblocks;

void handle_readout(struct board_def *bdef) {
    struct fdelay_time t;
    struct fd_time ts;
	uint32_t nsamples;
	int i,j;
    uint64_t picos;
    
    while( fdelay_read_raw(bdef->b, &t, 1, buf, &nsamples, O_NONBLOCK) == 1) {	    // while there are samples to read
		
		// data is now contained in buf, but we do nothing with it!
		
		// check for missing samples
		if ( next != t.seq_id  )
			printf("ERROR! seq_id = %06d but expected next_id = %06d \n",t.seq_id, next);
			
		next = t.seq_id+nsamples;
		if (next > 65535 )
			next = next % 65536; 
		nstamps += nsamples;
		//if (nsamples>nsamples_max)
		//	nsamples_max = nsamples;
		nblocks++;
    }
    if ( t.utc >= previous_utc+1 ) { // if more than one second elapsed since last printout
		printf(" f_in= %lli Hz. Got %lli blocks/s with %5.2f stamps/block \n", nstamps,nblocks,(double)nstamps/(double)nblocks);
		previous_utc = t.utc;
		nstamps = 0;
		nblocks = 0;
		fflush(stdout);
	}
}

int main(int argc, char *argv[])
{
	int i;
	int fd,maxfd = -1;
	fd_set allset, curset;
	
	i = fdelay_init();
	if (i < 0) {
		fprintf(stderr, "%s: fdelay_init(): %s\n", argv[0],
			strerror(errno));
		exit(1);
	} else if (i == 0) {
		fprintf(stderr, "%s: no boards found\n", argv[0]);
		exit(1);
	}

	FD_ZERO(&allset); // initialize file descriptor set

	memset(&my_board, 0, sizeof(my_board)); // initialize to zero
	
	//boards[0].in_use = 1;        
	my_board.hw_index = 0x0100;
	
	//for(i=0;i<MAX_BOARDS;i++) {
	//	if(boards[i].in_use) {
			printf("board hw_index = 0x%04x \n", my_board.hw_index);
	//		int fd;

			configure_board( &my_board );
			
			// this gives a file-descriptor to read ctrl data from
			// %s-0-0-ctrl
			fd = fdelay_fileno_tdc( my_board.b ); // fdelay-tdc.c
			
			my_board.fd_ctrl = fd;
			//FD_SET(fd, &allset);
			//maxfd = fd;
	//		if (fd > maxfd)
	//			maxfd = fd;
	//	}
	//}
	printf("entering infinite loop. \n", my_board.hw_index);
	int timeout = 10000;
	for(;;) {
		curset = allset;
		// block until there is data to read
		// http://stackoverflow.com/questions/4171270/select-function-in-socket-programming

		// int select( 	int nfds,  	fd_set *readfds,  	fd_set *writefds,  	fd_set *exceptfds, struct timeval *timeout);
		// 
		//if ( select(maxfd+1, &curset, NULL, NULL, NULL) <= 0) { 
			// my_board.fd_ctrl
		if ( poll( &fd, 1, timeout) <= 0) { 
			// we should not get here !?
			printf("ERROR poll() <=0\n");
			exit(-1);
			
			//fflush(stdout);
			//continue; /* signal handler did it... */
		}
		//for(i=0;i<MAX_BOARDS;i++) {
			//if(!boards[i].in_use)
			//	continue;
			//if (!FD_ISSET( my_board.fd_ctrl, &curset))
			//	continue;
			handle_readout( &my_board );
		//}
	}
}

