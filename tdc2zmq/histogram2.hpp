/* time-stamp correaltion histogram
 * AW 2014-02-26

 */

#pragma once

#include <zmq.hpp>
#include <string>
#include <iostream>
#include <cstdio> // printf()

#include <deque>

#include <math.h>  // floorf()
#include <sys/stat.h> // chmod()

#include "tdc.pb.h"

#include "ts.hpp"

// all-time intervals histogram
class Histogrammer2 {
	public:
		Histogrammer2() {
			tau.s = 1;
			tau.ps = 0;
			nbins = 100;
			//t0.s = -1; // uninitialized!
			
			hist = new std::vector<int>(nbins);
			count=0;
			deq.clear();
			binwidth = ((double)tau.s + (double)tau.ps/1e12)/(nbins-1);
		}
		Histogrammer2(TS tau0, int nbins0, TS gate0) {
			tau = tau0;
			gate = gate0;
			nbins = nbins0;

			hist = new std::vector<int>(nbins);
			count=0;
			binwidth = ((double)tau.s + (double)tau.ps/1e12)/(nbins-1);
			printf("     tau: %lli.%012lli \n", tau.s, tau.ps );
			printf("    gate: %lli.%012lli \n", gate.s, gate.ps );
			printf("binwidth: %0.12f \n", binwidth );
			deq.clear();
		}
		~Histogrammer2() {
			delete hist;
		};


		// add time-stamp
		void append(TS ts) {
			if ( deq.empty() ) { // special case at start when deq is empty
				printf("    ts0: %lli.%012lli \n", ts.s, ts.ps );
				std::vector<int> no_bins;
				deq.push_back( std::make_pair(ts, no_bins ) );
				return;
			}
            
			//printf("  append-ts: %lli.%012lli \n", ts.s, ts.ps );
			//printf("  deq.size(): %d \n", deq.size() );
			/*
			int deqn=0;
			for (deqType::iterator itr = deq.begin();itr!=deq.end();++itr) {
				printf("%d %lli.%012lli \n",(*itr).first.s, (*itr).first.ps, deqn);
				++deqn;
			}*/
			
			// histogram bins correspond to time:
		    // hist[0]      = 0 ... tau/bins
		    // hist[bins-1] = (bins-1)*tau/bins ... tau
		    deqType::reverse_iterator it = deq.rbegin();
		    TS diff;
		    std::vector<int> my_bins;
		    do {
				diff = ts - (*it).first;
				//printf("  diff: %lli.%012lli  %d \n", diff.s, diff.ps, tau > diff );
				if ( !(tau > diff) ) { // TS class could implement < also.. 
					break;
					//++it;
					//continue; // stop if we don't have a histogram bin for this stamp
				}
				double t = (double)diff.s + (double)diff.ps/1e12; // looses precision??
				int bin_number = floorf( t/binwidth );
				//printf("    diff: %lli.%012lli bin= %d\n", diff.s, diff.ps, bin_number );
				
				// sanity check on bin-number
				if (!( bin_number >= 0 ) || (bin_number >= nbins) )
					printf("ERROR bin_number=%d nbins=%d \n",bin_number,nbins);
				assert( bin_number >= 0 );
				assert( bin_number < nbins );

				
				(*hist)[bin_number] = (*hist)[bin_number] + 1;
				my_bins.push_back( bin_number );
				count++;
				

				++it;
			} while ( it!=deq.rend() );

			// store the new time-stamp in deq
            deq.push_back( std::make_pair(ts, my_bins ) );
            
            // remove elements from the deq, to keep only stamps within the gate-time
			while ( deq_full() && deq.size() > 1 ) {
				for (std::vector<int>::iterator it = deq.front().second.begin() ; 
				     it != deq.front().second.end(); ++it) {
					(*hist)[ *it ]--; // decrease histogram count for this element
					
				}
				count -= deq.front().second.size();
				deq.pop_front(); // remove elements
			}

		}
		// access the histogram
		int histogram_n(int nbin) {
			return (*hist)[nbin];
		};
		int get_bins() { return nbins; }

		int hcount() { return count; }
		
		bool deq_full() {
			if (deq.size()<=1)
				return false;
			if (  deq_delta() > gate ) {
				return true;
			} else
				return false;
		}
		int deq_size() {
			return deq.size();
		}
		TS deq_delta() {
			return deq.back().first - deq.front().first ;
		}
	private:
		TS tau;    // time-interval for histogram rightmost bin
		int nbins; // number of bins in the histogram
		double binwidth;
		int count; // number of counts in the histogram
		std::vector<int>* hist; // the histogram itself
		TS gate; // gate time
		typedef std::deque< std::pair<TS, std::vector<int> > > deqType;
		//std::deque< std::pair<TS, std::vector<int> > > 
		deqType deq; // deq of time-stamps and histogram-bins
		
};

// subscribe to Tstamp, and create histogram.
class TsHist2 {
	public:
		TsHist2() {
			context = new zmq::context_t(1); // what's the "1" ?
			subscriber = new zmq::socket_t(*context, ZMQ_SUB);
			subscriber->connect("ipc:///tmp/tstamp.pipe");			
			char messageType[] = { 0x0a, 0x02, 0x54, 0x54 }; // protobuf + "TT"			
			subscriber->setsockopt( ZMQ_SUBSCRIBE, messageType, 4 );
			

			// PUB socket
			publisher = new zmq::socket_t(*context, ZMQ_PUB);
			publisher->bind("ipc:///tmp/histogram2.pipe");
			// change permissions so everyone can read
			chmod( "/tmp/histogram2.pipe", S_IWOTH); // http://linux.die.net/man/3/chmod
			
			// 10ms = 100 Hz   =   (int64_t)10000000000
			// 1 ms = 1  kHz =      (int64_t)1000000000
			// 100us = 10 kHz =      (int64_t)100000000
			// 10us = 100 kHz =       (int64_t)10000000
			// 1 MHz = 1 us =          (int64_t)1000000
			// 12 000 048 Hz =           (int64_t)83333
			hist_mod = TS( (int64_t)0,(int64_t)100000000);
			hist_gate = TS(90,0);
			int n_bins = 4000;
			// this class does the work
			hist = new Histogrammer2( hist_mod, n_bins , hist_gate);
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
					printf("PUB[ %d ] : hist2 deq_count=%d hcount = %d  tau=%lli.%012lli elapsed=%lli.%012lli gate=%lli.%012lli  \n",  
					zmq_pub_msg.size(), hist->deq_size(), hist->hcount(), hist_mod.s, hist_mod.ps, 
					elapsed.s, elapsed.ps, hist_gate.s, hist_gate.ps );
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
		Histogrammer2* hist;
		
		TS hist_mod;
		TS hist_gate;
		TS last_calc;
		TS update_timeout;

};
