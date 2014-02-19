/* zmq SUB with protobuf 

 */

#pragma once

#include <zmq.hpp>
#include <string>
#include <iostream>
#include <cstdio> // printf()

#include <deque>

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
};

// simple frequency counter, with given gate-time
// count input events during the gate-time
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
			deq.push_front( ts );

			while ( deq_full() && deq.size() > 1 ) {
				deq.pop_back(); // remove elements at back
			}
			std::cout << " gate = " << gate.s << " deq.size() = " << deq.size();
			printf(" f= %6.012f \n", f() );
		}
		// deq is full when timestamps span more than the gate
		bool deq_full() {
			if (deq.size()==0)
				return false;			
			if (  ( deq.front()-deq.back() ) > gate ) {
				//std::cout << " deq full!\n";
				return true;
			} else
				return false;
		}
		// calculate frequency of deq
		double f() {
			if (deq.size()<=1)
				return 0;
			TS diff = ( deq.front()-deq.back() );
			double freq = (diff.s + diff.ps/(1e12) )/ (deq.size()-1 );
			return freq;
		}
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
			cnt = new GateCounter( TS(20,0) );
		};
		
		void sub() {
			zmq::message_t* zmq_msg = new zmq::message_t();
			
			std::cout << "ZMQ SUB started.\n";
			
			while (1) {
				socket->recv(zmq_msg);		
				printf("SUB[ %d ] :",  zmq_msg->size() );

				// interpret bytes with protobuf
				pb_msg.ParseFromArray( (char *)zmq_msg->data(), zmq_msg->size() );
				std::cout << "type=" << pb_msg.messagetype() << " n="<< pb_msg.n() << "\n"; 
				for (int n = 0; n<pb_msg.utc_size(); n++) {
					
					printf(" %i stamp: %lli.%012lli \n", n, pb_msg.utc(n), pb_msg.ps(n) );
					cnt->append( TS( pb_msg.utc(n), pb_msg.ps(n) ) );
				}
				
			}
			delete zmq_msg;
		};
	private:
		zmq::context_t* context;
		zmq::socket_t* socket;
		StampBlock pb_msg; // protobuf message type
		GateCounter* cnt;
};
