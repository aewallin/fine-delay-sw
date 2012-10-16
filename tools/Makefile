# user-space tools for spec-fine-delay

M = $(shell /bin/pwd)/../kernel

HOST_EXTRACFLAGS += -I$(M) -I../zio/include -Wno-trigraphs -Wall -ggdb

HOSTCC ?= gcc

hostprogs-y := fd-raw-input
hostprogs-y += fd-raw-gettime
hostprogs-y += fd-raw-settime
hostprogs-y += parport-burst
hostprogs-y += fd-raw-output
hostprogs-y += fd-raw-perf

# we are not in the kernel, so we need to piggy-back on "make modules"
all modules: $(hostprogs-y)

clean:
	rm -f $(hostprogs-y) *.o *~

# make nothing for module_install, but avoid errors
module_install:

# we need this as we are out of the kernel
%: %.c
	$(HOSTCC) $(HOST_EXTRACFLAGS) -O2 -Wall $^ -o $@
