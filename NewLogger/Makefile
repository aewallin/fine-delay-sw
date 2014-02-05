CFLAGS=-I../lib -I../kernel -g
LDFLAGS=-L../lib -L../kernel -lfdelay

all:	fdelay-gs fdelay-dumplog

fdelay-gs: fdelay-gs.o
	gcc -o $@ $^ $(LDFLAGS)

fdelay-dumplog: fdelay-dumplog.o
	gcc -o $@ $^ $(LDFLAGS)


fdelay-gs.rusleep: fdelay-gs.rusleep.o
	gcc -o $@ $^ $(LDFLAGS)
