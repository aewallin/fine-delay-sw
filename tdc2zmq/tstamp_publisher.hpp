/* zmq PUB with protobuf 

 */

#pragma once

#include <zmq.hpp>
#include <string>
#include <iostream>
#include <cstdio> // printf()

#include <sys/stat.h> // chmod()

#include "tdc.pb.h"


#include "ts.hpp"


// time-stamp publisher
class Tspub {
	public:
		Tspub() {
			context = new zmq::context_t(1); // what's the "1" ?
			socket = new zmq::socket_t(*context, ZMQ_PUB);
			//socket->bind("tcp://*:5555"); // tcp transport

			socket->bind("ipc:///tmp/tstamp.pipe");
			// change permissions so everyone can read
			chmod( "/tmp/tstamp.pipe", S_IWOTH); // http://linux.die.net/man/3/chmod
			pb_msg.set_messagetype( "TT"); 
			
			stamp_current=0;
			stamp_timeout=TS(0,10000000000); // 10ms = 100 Hz
			stamp_blocksize=1000; // block size limit. 100khz / 10Hz = 1000
			sendtime = TS(0,0);
			pb_msg_clear();
		};
		bool timeout(TS& t0) {
			TS elapsed = t0 - sendtime;
			//float elapsed = ( 1000*(float)t/CLOCKS_PER_SEC );
			
			if (elapsed > stamp_timeout) {
				printf("elapsed = %lli.%012lli \n", elapsed.s,elapsed.ps);
				sendtime = t0;
				return true;
			}
			return false;
		}

		
/* each raw sample is in fd_time format (see fine-delay.h)
struct fd_time {
	uint64_t utc;      // 8 bytes
	uint32_t coarse;   // 4
	uint32_t frac;     // 4
	uint32_t channel;  // 4
	uint32_t seq_id;   // 4
}; */
		// seconds of raw time-stamp at position j in the buffer
		uint64_t stamp_to_s(int j, unsigned char* buf) {
				return  (uint64_t)(buf[j*24]) + 
						 (uint64_t)(buf[j*24+1]<<8)+
						 (uint64_t)(buf[j*24+2]<<16)+
						 (uint64_t)(buf[j*24+3]<<24)+
						 (uint64_t)(buf[j*24+4]<<32)+ // this byte, and onwards, 
						 (uint64_t)(buf[j*24+5]<<40)+ // will be zero in the foreseeable future..
						 (uint64_t)(buf[j*24+6]<<48)+ // zero
						 (uint64_t)(buf[j*24+7]<<56); // zero

		}
		// picoseconds of raw time-stamp at position j in the buffer
		uint64_t stamp_to_ps(int j, unsigned char* buf) {
				uint32_t coarse = (uint32_t)buf[j*24+8] + (uint32_t)(buf[j*24+9]<<8) + 
						   (uint32_t)(buf[j*24+10]<<16) + (uint32_t)(buf[j*24+11]<<24);
				uint32_t frac = (uint32_t)buf[j*24+12] + (uint32_t)(buf[j*24+13]<<8)+
						  (uint32_t)(buf[j*24+14]<<16) + (uint32_t)(buf[j*24+15]<<24);
				
				// frac is 1/4096 fractions of the 8 ns clock.
				// coarse is number of tics of the 125 MHz fpga clock == 8ns == 8000 ps

			return (uint64_t)frac * 8000 / 4096 + (uint64_t)coarse * 8000;
		}
		uint32_t stamp_to_id(int j, unsigned char* buf) {
			return (uint32_t)buf[j*24+20] + (uint32_t)(buf[j*24+21]<<8)+ 
			            (uint32_t)(buf[j*24+22]<<16) + (uint32_t)(buf[j*24+23]<<24);
		}
		
		// receive new time-stamps from hardware
		void pub(int n, unsigned char* buf) {

			/*
			if (!initialized) {
				unpack_stamp(t0,0,buf);
				printf("   t0=  %lli ", t0.utc );
				initialized = true;
			}*/

			pb_msg.set_n( pb_msg.n() + n ); // number of stamps in this block
			//pb_msg.clear_utc(); // clear the utc-array
			//pb_msg.clear_ps(); // clear picoseconds
			//pb_msg.clear_id(); // clear ids
			for (int j = 0; j < n; j++) { // loop over raw 24-byte timestamps
				pb_msg.add_utc( stamp_to_s(j,buf) );
				pb_msg.add_ps( stamp_to_ps(j,buf) );
				pb_msg.add_id( stamp_to_id(j,buf) );
				//printf("n=%d \t id=%d %lli.%012lli \n",n,stamp_to_id(j,buf), stamp_to_s(j,buf) ,stamp_to_ps(j,buf) );
			}
			TS last_stamp = TS(stamp_to_s(n-1,buf), stamp_to_ps(n-1,buf) );
			if ( timeout( last_stamp ) ) {
				zmq::message_t zmq_msg( pb_msg.ByteSize() );
				pb_msg.SerializeToArray( (void *)zmq_msg.data(), pb_msg.ByteSize() );
				socket->send( zmq_msg );
				
				std::cout << "PUB[ nstamps=" << pb_msg.n() << " ] bytes=" << pb_msg.ByteSize() << "\n";
				pb_msg_clear();
			}
			//std::cout << "PUB[ nstamps=" << n << " ] bytes=" << pb_msg.ByteSize() << "\n";
		}
		void pb_msg_clear() {
			pb_msg.set_n( 0 );
			pb_msg.clear_utc(); // clear the utc-array
			pb_msg.clear_ps(); // clear picoseconds
			pb_msg.clear_id(); // clear ids

		}
	private:
		zmq::context_t* context;
		zmq::socket_t* socket;
		//zmq::message_t zmq_msg;
		StampBlock pb_msg; // protobuf message type

		int stamp_current;
		int stamp_blocksize; // send a ZMQ message if we have this many stamps
		TS stamp_timeout; // send a ZMQ message if timeout milliseconds has passed
		TS sendtime;
		//boost::timer::cpu_timer cpu_timer;

		//fd_time ts;
		//fd_time t0; // first time-stamp seen, use as offset for all others
		//bool initialized;
};
