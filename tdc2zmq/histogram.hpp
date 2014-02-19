/* zmq SUB with protobuf 

 */

#pragma once

#include <zmq.hpp>
#include <string>
#include <iostream>
#include <cstdio> // printf()

//#include <deque>
#include <math.h>  // floorf()

#include "tdc.pb.h"

class TS {
public:
	int64_t s;
	int64_t ps;
	TS() {s=0;ps=0;};
	TS(int64_t sec, int64_t pico) { // fixme: use initializer list
		s = sec;
		ps = pico;
	};
	// subtract t
	TS &operator-=(const TS &t) {
		//std::cout << " first " << s << "." << ps << "\n";
		//std::cout << " second" << t.s << "." << t.ps << "\n";
		//printf("  first: %lli.%012lli \n", s, ps );
		//printf(" second: %lli.%012lli \n", t.s, t.ps );
	    if (ps >= t.ps) {
			ps -= t.ps;
		} else {
			ps = 1e12 + ps - t.ps;
			s--;
		}
		s -= t.s;
		//printf("   diff: %lli.%012lli \n", s, ps );
		return *this;
	};
	
	const TS operator-(TS &t) const {
		return TS(*this) -= t;
	};
	bool operator>(const TS &t) const {
		//printf(">  first: %lli.%012lli %d %d\n", s, ps , s>t.s, ps>t.ps);
		//printf("> second: %lli.%012lli \n", t.s, t.ps );
		if (s>=t.s)
			return true;
		else if (s < t.s)
			return false;
		else if (ps > t.ps) // here s==t.s
			return true;
		return false;
	};
	TS operator%(const TS &t) const {
		printf("     ts: %lli.%012lli \n", s, ps );
		printf("     tau: %lli.%012lli \n", t.s, t.ps );
		
		int64_t smod;
		if (t.s > 0 ) 
			smod = s % t.s;
		else
			smod = s;
		int64_t psmod;
		if (t.ps > 0 )
			psmod = ps % t.ps;
		else
			psmod = ps;
		printf("mod(ts,tau): %lli.%012lli \n", smod, psmod );
		
		return TS(smod,psmod);
	};
};

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
		}
		Histogrammer(TS tau0, int nbins) {
			tau.s = tau0.s;
			tau.ps = tau0.ps;
			t0.s = -1; // uninitialized!
			bins = nbins;
			hist = new std::vector<int>(bins);
		}
		~Histogrammer() {
			delete hist;
		};
		
		// add time-stamp
		void append(TS ts) {
			TS modt = ts % tau ; // modulus operator of TS!
			// find the bin where modt belongs
			printf("  append: %lli.%012lli \n", ts.s, ts.ps );
			
			// histogram bins correspond to time:
		    // hist[0]      = 0 ... tau/bins
		    // hist[bins-1] = (bins-1)*tau/bins ... tau
            double t = modt.s + modt.ps/1e12; // looses precision??
            double binwidth = (tau.s + tau.ps/1e12)/bins;
            int bin_number = floorf( t/binwidth );
            assert( bin_number >= 0 );
            assert( bin_number < bins );
            (*hist)[bin_number] = (*hist)[bin_number] + 1;	
            printf("    modt: %lli.%012lli bin= %d\n", modt.s, modt.ps , bin_number);	
		}
		// access the histogram
		int histogram_n(int nbin) {
			return (*hist)[nbin];
		};
		int get_bins() { return bins; }
	private:
		TS tau; // histogram modulo this time-interval
		TS t0; // first observation
		int bins; // number of bins in the histogram
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
			
			publisher = new zmq::socket_t(*context, ZMQ_PUB);
			publisher->bind("ipc:///tmp/histogram.pipe");
			
			
			// this class does the work
			hist = new Histogrammer( TS(20,0), 100 );
		};
		
		void sub() {
			zmq::message_t* zmq_msg = new zmq::message_t();
			
			std::cout << "ZMQ SUB started.\n";
			
			while (1) {
				// Subscribe to time-stamps
				subscriber->recv(zmq_msg);		
				printf("SUB[ %d ] :",  zmq_msg->size() );
				pb_msg.ParseFromArray( (char *)zmq_msg->data(), zmq_msg->size() );
				std::cout << "type=" << pb_msg.messagetype() << " n="<< pb_msg.n() << "\n"; 
				for (int n = 0; n<pb_msg.utc_size(); n++) {
					
					printf(" %i stamp: %lli.%012lli \n", n, pb_msg.utc(n), pb_msg.ps(n) );
					hist->append( TS( pb_msg.utc(n), pb_msg.ps(n) ) );
				}
				
				// Publish histogram
				pb_hist_msg.set_bins( hist->get_bins() );
				pb_hist_msg.clear_hist();
				for (int n = 0; n< hist->get_bins() ; n++ ) {
					pb_hist_msg.add_hist(  hist->histogram_n(n) );
				}
				zmq::message_t zmq_pub_msg( pb_hist_msg.ByteSize() );
				pb_hist_msg.SerializeToArray( (void *)zmq_pub_msg.data(), pb_hist_msg.ByteSize() );
				
				// actual send , , msg.ByteSize()
				publisher->send( zmq_pub_msg );
				
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
};
