# This is not a kbuild Makefile. It is a plain Makefile so it can be copied

LIB = libfdelay.a
LOBJ := fdelay-init.o

CFLAGS = -Wall -ggdb -O2 -I.. -I$(ZIO)/include
LDFLAGS = -L. -lfdelay

DEMOSRC = fdelay-list.c
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

-include .depend

