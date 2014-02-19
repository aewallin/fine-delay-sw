/* zmq PUB with protobuf 

 */

#pragma once

#include <zmq.hpp>
#include <string>
#include <iostream>
#include <cstdio> // printf()

#include <sys/stat.h> // chmod()

#include "tdc.pb.h"

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

		};
		
		
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
			return frac * 8000 / 4096 + coarse * 8000;
		}

		void pub(int n, unsigned char* buf) {
			
			/*
			if (!initialized) {
				unpack_stamp(t0,0,buf);
				printf("   t0=  %lli ", t0.utc );
				initialized = true;
			}*/
			pb_msg.set_n( n ); // number of stamps in this block
			pb_msg.clear_utc(); // clear the utc-array
			pb_msg.clear_ps(); // clear picoseconds
			
			for (int j = 0; j < n; j++) { // loop over raw 24-byte timestamps
				pb_msg.add_utc( stamp_to_s(j,buf) );
				pb_msg.add_ps( stamp_to_ps(j,buf) );
			}

			zmq::message_t zmq_msg( pb_msg.ByteSize() );
			pb_msg.SerializeToArray( (void *)zmq_msg.data(), pb_msg.ByteSize() );
			std::cout << "PUB[ nstamps=" << n << " ] bytes=" << pb_msg.ByteSize() << "\n";
			socket->send( zmq_msg );
		}
	private:
		zmq::context_t* context;
		zmq::socket_t* socket;
		//zmq::message_t zmq_msg;
		StampBlock pb_msg; // protobuf message type
		//fd_time ts;
		//fd_time t0; // first time-stamp seen, use as offset for all others
		//bool initialized;
};
