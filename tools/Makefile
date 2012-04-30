# user-space tools for spec-fine-delay

HOST_EXTRACFLAGS += -I$(M) -I$(ZIO)/include -Wno-trigraphs

HOSTCC ?= gcc

hostprogs-y := fd-raw-input
hostprogs-y += parport-burst

# we are not in the kernel, so we need to piggy-back on "make modules"
modules: $(hostprogs-y)

clean:
	rm -f $(hostprogs-y) *.o *~

# make nothing for module_install, but avoid errors
module_install:

# we need this as we are out of the kernel
%: %.c
	$(HOSTCC) $(HOST_EXTRACFLAGS) -Wall $^ -o $@
