
LINUX ?= /lib/modules/$(shell uname -r)/build

ZIO ?= $(HOME)/zio
SPEC ?= $(HOME)/spec-sw

ccflags-y = -I$(ZIO)/include -I$(SPEC)/kernel

obj-m := spec-fine-delay.o

spec-fine-delay-objs = fd-lib.o fd-zio.o fd-probe.o

all modules:
	$(MAKE) -C $(LINUX) M=$(shell /bin/pwd) modules

clean: