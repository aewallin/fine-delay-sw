/* zmq SUB with protobuf 

 */

#pragma once

#include <zmq.hpp>
#include <string>
#include <iostream>
#include <cstdio> // printf()

#include <deque>

#include <sys/stat.h> // chmod()

#include "tdc.pb.h"
#include "ts.hpp"


// simple frequency counter, with given gate-time
// count input events during the gate-time
// calculate a new f-output every gate-time seconds.
class GateCounter {	

	public:
		GateCounter() {
			gate.s = 5;
			gate.ps = 0;
		}
		GateCounter(TS g) {
			gate.s = g.s;
			gate.ps = g.ps;
		}
		// add time-stamp
		void append(TS ts) {

			deq.push_back( ts );
			//printf(" deq.append  id=%d ts=%lli.%012lli \n", ts.id, ts.s, ts.ps );
			TS diff = ( deq.back()-deq.front() );
			if ( !deq.empty() ) {
				assert( ts > deq.back() ); // new time-stamp should be larger than previous!
				if (!(diff > TS(0,0)))
					printf("ERROR diff=back-front=%lli.%012lli -%lli.%012lli =%lli.%012lli  ",
					deq.back().s, deq.back().ps, deq.front().s, deq.front().ps, diff.s, diff.ps);
				assert( diff > TS(0,0) );
			}


			//printf(" deq.diff  = %lli.%012lli \n", diff.s, diff.ps );
			while ( deq_full() && deq.size() > 1 ) {
				deq.pop_front(); // remove elements at back
			}
			// f();
		}
		// deq is full when timestamps span more than the gate
		bool deq_full() {
			if (deq.size()<=1)
				return false;			
			//TS diff = ( deq.back()-deq.front() );
			//printf(" %d diff  = %lli.%012lli \n", deq.size(), diff.s, diff.ps );
			//printf("    gate  = %lli.%012lli \n",  gate.s, gate.ps );
			//printf("    test = %d \n", ( diff ) > gate );
			if (  deq_delta() > gate ) {

				//std::cout << " deq full!\n";
				return true;
			} else
				return false;
		}
		// calculate frequency of deq
		double f() {
			if (deq.size()<=1)
				return 0;

			//printf(" front = %lli.%012lli \n", deq.front().s, deq.front().ps );
			//printf(" back  = %lli.%012lli \n", deq.back().s, deq.back().ps );
			TS diff = ( deq.back()-deq.front() );
			double freq = (double)(deq.size()-1 ) / ((double)diff.s + (double)diff.ps/(1e12) );
			// printf(" g = %012lli n=%d diff=%lli.%012lli f=%6.012f \n", gate.ps, deq.size(), diff.s, diff.ps,freq);
			// << gate.s << " deq.size() = " << deq.size();
			//printf(" f= %6.012f \n", f() );
			//printf(" deq.diff = %lli.%012lli \n", diff.s,diff.ps );
			return freq;
		}
		int deq_size() {
			return deq.size();
		}
		TS deq_delta() {
			return deq.back()-deq.front() ;
		}
		TS gatet() { return gate; };

	private:
		TS gate;
		std::deque<TS> deq;
};

class Tssub {
	public:
		Tssub() {
			context = new zmq::context_t(1); // what's the "1" ?
			socket = new zmq::socket_t(*context, ZMQ_SUB);


			socket->connect("ipc:///tmp/tstamp.pipe");
			char messageType[] = { 0x0a, 0x02, 0x54, 0x54 }; // protobuf + "TT"
			socket->setsockopt( ZMQ_SUBSCRIBE, messageType, 4 );
			
			// PUB socket
			publisher = new zmq::socket_t(*context, ZMQ_PUB);
			publisher->bind("ipc:///tmp/frequency.pipe");
			// change permissions so everyone can read
			chmod( "/tmp/frequency.pipe", S_IWOTH); // http://linux.die.net/man/3/chmod
			
			
			// counter
			gate = TS(1,0); //TS(0,5e11);
			cnt = new GateCounter( gate );
			prev_id = -1;
			last_f_calc = TS(0,0);


		};
		
		void sub() {
			zmq::message_t* zmq_msg = new zmq::message_t();
			
			std::cout << "ZMQ SUB started.\n";
			
			while (1) {

				socket->recv(zmq_msg);
				//printf("SUB[ %d ] :",  zmq_msg->size() );

				// interpret bytes with protobuf
				pb_msg.ParseFromArray( (char *)zmq_msg->data(), zmq_msg->size() );
				//std::cout << "type=" << pb_msg.messagetype() << " n="<< pb_msg.n() << "\n";
				int nstamps = pb_msg.utc_size();
				for (int n = 0; n<nstamps; n++) {
					// check that we are not missing stamps.
					if (prev_id == -1) // uninitialized
						prev_id = pb_msg.id(n);
					else {
						if( ( (prev_id + 1) % 65536) != pb_msg.id(n) ) {
							printf("ERROR! prev_id=%d current=%d",prev_id, pb_msg.id(n));
							assert( ( (prev_id +1) % 65536) == pb_msg.id(n) );
						}
						prev_id = pb_msg.id(n);
					}
					
					//printf(" %i stamp: %lli.%012lli \n", n, pb_msg.utc(n), pb_msg.ps(n) );
					cnt->append( TS( pb_msg.utc(n), pb_msg.ps(n), pb_msg.id(n) ) );
					
					//printf("%d %d %d \n",n, prev_id, pb_msg.id(n) );
				}
				
				TS last_stamp( pb_msg.utc(nstamps-1), pb_msg.ps(nstamps-1), pb_msg.id(nstamps-1) );
				TS elapsed = (last_stamp - last_f_calc);
				if ( elapsed > gate ) {
					// frequency calculation

					double f = cnt->f();
					printf("f= %.06f deqN=%d deltaT=%lli.%012lli gate=%lli.%012lli\n", 
					f, cnt->deq_size(), cnt->deq_delta().s, cnt->deq_delta().ps,
					 cnt->gatet().s, cnt->gatet().ps );
					
					pb_f_msg.set_f( f);
					pb_f_msg.set_s( last_stamp.s );
					pb_f_msg.set_ps( last_stamp.ps );

					zmq::message_t zmq_pub_msg( pb_f_msg.ByteSize() );
					pb_f_msg.SerializeToArray( (void *)zmq_pub_msg.data(), pb_f_msg.ByteSize() );
					printf("PUB[ %d ] : ", // hcount = %d  mod=%lli.%012lli elapsed=%lli.%012lli\n",  
					zmq_pub_msg.size() ); //, hist->hcount(), hist_mod.s, hist_mod.ps, elapsed.s, elapsed.ps );
					fflush(stdout);
					// actual send , , msg.ByteSize()
					publisher->send( zmq_pub_msg );
					
					
					 

					//printf("last ts = %lli.%012lli \n", last_stamp.s,last_stamp.ps);
					//printf("last f  = %lli.%012lli \n", last_f_calc.s,last_f_calc.ps);
					//printf("elapsed = %lli.%012lli \n", elapsed.s,elapsed.ps);
					fflush(stdout);
					last_f_calc = last_stamp;

				}
				
			}
			delete zmq_msg;
		};
	private:
		zmq::context_t* context;

		zmq::socket_t* socket; // for SUB
		zmq::socket_t* publisher;
		
		StampBlock pb_msg; // protobuf message type
		Frequency  pb_f_msg; // frequency message
		GateCounter* cnt;
		TS last_f_calc; // the last time we called cnt->f() for output
		TS gate;
		int prev_id;

};
