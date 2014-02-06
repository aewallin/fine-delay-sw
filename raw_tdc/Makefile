CFLAGS=-I../lib -I../kernel -I../zio/include -g
LDFLAGS=-L../lib -L../kernel -lfdelay

all:	tdc_raw_dump speed_test

tdc_raw_dump: tdc_raw_dump.o
	gcc -o $@ $^ $(LDFLAGS)
	
speed_test: speed_test.o
	gcc -o $@ $^ $(LDFLAGS)
	
clean:
	rm -rf *o tdc_raw_dump speed_test
