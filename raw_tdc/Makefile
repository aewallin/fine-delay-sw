CFLAGS=-I../lib -I../kernel -I../zio/include -g
LDFLAGS=-L../lib -L../kernel -lfdelay

all:	tdc_raw_dump

tdc_raw_dump: tdc_raw_dump.o
	gcc -o $@ $^ $(LDFLAGS)
