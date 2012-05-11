CFLAGS=-I../lib -I.. -g
LDFLAGS=-L../lib -lfdelay

all:	fdelay-gs fdelay-dumplog fdelay-gs.ru fdelay-gs.rusleep

fdelay-gs: fdelay-gs.o
	gcc -o $@ $^ $(LDFLAGS)

fdelay-dumplog: fdelay-dumplog.o
	gcc -o $@ $^ $(LDFLAGS)


fdelay-gs.rusleep: fdelay-gs.rusleep.o
	gcc -o $@ $^ $(LDFLAGS)
