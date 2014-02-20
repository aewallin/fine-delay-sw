/* zmq SUB with protobuf 

 */

#pragma once

#include <zmq.hpp>
#include <string>
#include <iostream>
#include <cstdio> // printf()

#include <math.h>  // floorf()
#include <sys/stat.h> // chmod()

#include "tdc.pb.h"

#include "ts.hpp"



// simple frequency counter, with given gate-time
// count input events during the gate-time
class Histogrammer {
	public:
		Histogrammer() {
			tau.s = 1;
			tau.ps = 0;
			t0.s = -1; // uninitialized!
			bins = 100;
			hist = new std::vector<int>(bins);
			count=0;
			
		}
		Histogrammer(TS tau0, int nbins) {
			tau.s = tau0.s;
			tau.ps = tau0.ps;
			t0.s = -1; // uninitialized!
			bins = nbins;
			hist = new std::vector<int>(bins);
			count=0;
			printf("     tau0: %lli.%012lli \n", tau0.s, tau0.ps );
			printf("     tau: %lli.%012lli \n", tau.s, tau.ps );
		}
		~Histogrammer() {
			delete hist;
		};
		
		// add time-stamp
		void append(TS ts) {
			TS modt = ts % tau ; // modulus operator of TS!
			//printf("  append: %lli.%012lli \n", ts.s, ts.ps );
			//printf("    modt: %lli.%012lli \n", modt.s, modt.ps );
			//printf("     tau: %lli.%012lli \n", tau.s, tau.ps );
			
			// find the bin where modt belongs
			// histogram bins correspond to time:
		    // hist[0]      = 0 ... tau/bins
		    // hist[bins-1] = (bins-1)*tau/bins ... tau
            double t = modt.s + modt.ps/1e12; // looses precision??
            double binwidth = ((double)tau.s + (double)tau.ps/1e12)/bins;
            int bin_number = floorf( t/binwidth );
            if (!( bin_number >= 0 ))
				printf("ERROR bin_number=%d \n",bin_number);
            assert( bin_number >= 0 );
            assert( bin_number < bins );
            (*hist)[bin_number] = (*hist)[bin_number] + 1;
            count++;
            //printf("    modt: %lli.%012lli bin= %d\n", modt.s, modt.ps , bin_number);	
		}
		// access the histogram
		int histogram_n(int nbin) {
			return (*hist)[nbin];
		};
		int get_bins() { return bins; }
		int hcount() {
			return count;
		}
	private:
		TS tau; // histogram modulo this time-interval
		TS t0; // first observation
		int bins; // number of bins in the histogram
		int count; // number of counts in the histogram
		std::vector<int>* hist; // the histogram itself

};

// subscribe to Tstamp, and create histogram.
class TsHist {
	public:
		TsHist() {
			context = new zmq::context_t(1); // what's the "1" ?
			subscriber = new zmq::socket_t(*context, ZMQ_SUB);
			subscriber->connect("ipc:///tmp/tstamp.pipe");			
			char messageType[] = { 0x0a, 0x02, 0x54, 0x54 }; // protobuf + "TT"			
			subscriber->setsockopt( ZMQ_SUBSCRIBE, messageType, 4 );
			
			// PUB socket
			publisher = new zmq::socket_t(*context, ZMQ_PUB);
			publisher->bind("ipc:///tmp/histogram.pipe");
			// change permissions so everyone can read
			chmod( "/tmp/histogram.pipe", S_IWOTH); // http://linux.die.net/man/3/chmod
			
			// 10ms = 100Hz =   (int64_t)10000000000
			// 100us = 1 kHz =    (int64_t)100000000
			hist_mod = TS( (int64_t)0,(int64_t)100000000);
			// this class does the work
			hist = new Histogrammer( hist_mod, 10000 );
			update_timeout = TS(2,0); // update once/twice per second
			last_calc = TS(0,0);
		};
		
		void sub() {
			zmq::message_t* zmq_msg = new zmq::message_t();
			
			std::cout << "ZMQ SUB started.\n";
			
			while (1) {
				// Subscribe to time-stamps
				subscriber->recv(zmq_msg);
				//printf("SUB[ %d ] :",  zmq_msg->size() );
				pb_msg.ParseFromArray( (char *)zmq_msg->data(), zmq_msg->size() );
				//std::cout << "type=" << pb_msg.messagetype() << " n="<< pb_msg.n() << "\n"; 
				int nstamps = pb_msg.utc_size();
				for (int n = 0; n<nstamps; n++) {
					// printf(" %i stamp: %lli.%012lli \n", n, pb_msg.utc(n), pb_msg.ps(n) );
					hist->append( TS( pb_msg.utc(n), pb_msg.ps(n) ) );
				}
				
				TS last_stamp( pb_msg.utc(nstamps-1), pb_msg.ps(nstamps-1), pb_msg.id(nstamps-1) );
				TS elapsed = (last_stamp - last_calc);
				
				// Publish histogram
				if ( elapsed > update_timeout ) {
					pb_hist_msg.set_bins( hist->get_bins() );
					pb_hist_msg.set_s( hist_mod.s );
					pb_hist_msg.set_ps( hist_mod.ps );
					pb_hist_msg.clear_hist();
					for (int n = 0; n< hist->get_bins() ; n++ ) {
						pb_hist_msg.add_hist(  hist->histogram_n(n) );
					}
					zmq::message_t zmq_pub_msg( pb_hist_msg.ByteSize() );
					pb_hist_msg.SerializeToArray( (void *)zmq_pub_msg.data(), pb_hist_msg.ByteSize() );
					printf("PUB[ %d ] : hcount = %d  mod=%lli.%012lli elapsed=%lli.%012lli\n",  
					zmq_pub_msg.size(), hist->hcount(), hist_mod.s, hist_mod.ps, elapsed.s, elapsed.ps );
					fflush(stdout);
					// actual send , , msg.ByteSize()
					publisher->send( zmq_pub_msg );
					last_calc = last_stamp;
				}
			}
			delete zmq_msg;
		};
	private:
		zmq::context_t* context;
		zmq::socket_t* subscriber;
		zmq::socket_t* publisher;
		StampBlock pb_msg; // protobuf time-stamp message type
		Histogram pb_hist_msg; // protobuf histogram message type
		Histogrammer* hist;
		TS hist_mod;
		TS last_calc;
		TS update_timeout;
};
