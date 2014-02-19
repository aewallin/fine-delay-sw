cmake_minimum_required(VERSION 2.6)
 
Project(tdc2zmq)
CMAKE_POLICY(SET CMP0012 NEW)

set( CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR} ) # FindZeroMQ.cmake is in the current src dir!
FIND_PACKAGE(ZeroMQ REQUIRED)
IF( ${ZeroMQ_FOUND} )
	MESSAGE( STATUS "ZEROMQ include: " ${ZeroMQ_INCLUDE_DIRS} )
	MESSAGE( STATUS "ZEROMQ     lib: " ${ZeroMQ_LIBRARIES} )
ENDIF()
# ZeroMQ_FOUND - system has libzmq
# ZeroMQ_INCLUDE_DIRS - the libzmq include directories
# ZeroMQ_LIBRARIES - link these to use libzmq

FIND_PACKAGE(Protobuf REQUIRED)
#   PROTOBUF_FOUND - Found the Google Protocol Buffers library
#   PROTOBUF_INCLUDE_DIRS - Include directories for Google Protocol Buffers
#   PROTOBUF_LIBRARIES - The protobuf library
#
# The following cache variables are also defined:
#   PROTOBUF_LIBRARY - The protobuf library
#   PROTOBUF_PROTOC_LIBRARY   - The protoc library
#   PROTOBUF_INCLUDE_DIR - The include directory for protocol buffers
#   PROTOBUF_PROTOC_EXECUTABLE - The protoc compiler
IF( ${PROTOBUF_FOUND} )
	MESSAGE( STATUS "Protobuf  include: " ${PROTOBUF_INCLUDE_DIRS} )
	MESSAGE( STATUS "Protobuf      lib: " ${PROTOBUF_LIBRARIES} )
ENDIF()
INCLUDE_DIRECTORIES(${INCLUDE_DIRECTORIES} ${PROTOBUF_INCLUDE_DIRS} ${ZeroMQ_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
set( PROTO_SRCS 1) # what are these filenames??
set( PROTO_HDRS 1) # what are these filenames??

PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS "tdc.proto")
#add_library(mymessage mymessage.pb-c.c)
#target_link_libraries(mymessage ${PROTOBUF_LIBRARIES}) 

# from raw Makefile
# CFLAGS=-I../lib -I../kernel -I../zio/include -g
# LDFLAGS=-L../lib -L../kernel -lfdelay
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../lib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../kernel)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../zio/include)

# hardcoded location of fdelay-lib
FIND_LIBRARY(FDELAY_LIBRARY fdelay ${CMAKE_CURRENT_SOURCE_DIR}/../lib/)


ADD_EXECUTABLE       (tdc2zmq  tdc2zmq.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(tdc2zmq  ${FDELAY_LIBRARY} ${PROTOBUF_LIBRARY} ${ZeroMQ_LIBRARIES} ) 

# average frequency counter
ADD_EXECUTABLE       (fcounter  fcounter.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(fcounter  ${PROTOBUF_LIBRARY} ${ZeroMQ_LIBRARIES} ) 

# modulo-t histogram calculation
ADD_EXECUTABLE       (histogram  histogram.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(histogram  ${PROTOBUF_LIBRARY} ${ZeroMQ_LIBRARIES} ) 
