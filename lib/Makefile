# This is not a kbuild Makefile. It is a plain Makefile so it can be copied

LIB = libfdelay.a
LOBJ := fdelay-init.o
LOBJ += fdelay-time.o
LOBJ += fdelay-tdc.o
LOBJ += fdelay-output.o

CFLAGS = -Wall -ggdb -O2 -I../kernel -I../zio/include
LDFLAGS = -L. -lfdelay

DEMOSRC := fdelay-list.c
DEMOSRC += fdelay-board-time.c
DEMOSRC += fdelay-term.c
DEMOSRC += fdelay-read.c
DEMOSRC += fdelay-fread.c
DEMOSRC += fdelay-pulse.c
DEMOSRC += fdelay-open-by-lun.c
DEMOSRC += fdelay-pulse-tom.c

DEMOS := $(DEMOSRC:.c=)

modules all: lib demos

lib: $(LIB)

demos: $(DEMOS)

%: %.c $(LIB)
	$(CC) $(CFLAGS) $*.c $(LDFLAGS) -o $@

$(LIB): $(LOBJ)
	ar r $@ $^

clean:
	rm -f $(LIB) .depend *.o *~

.depend: Makefile $(wildcard *.c *.h ../*.h)
	$(CC) $(CFLAGS) -M $(LOBJ:.o=.c) -o $@

install modules_install:

-include .depend

