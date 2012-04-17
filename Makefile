
LINUX ?= /lib/modules/$(shell uname -r)/build
ZIO ?= $(HOME)/zio
SPEC_SW ?= $(HOME)/spec-sw/kernel

ccflags-y = -I$(ZIO)/include -I$(SPEC_SW)/kernel -I$M

ccflags-y += -DDEBUG # temporary

obj-m := spec-fine-delay.o

spec-fine-delay-objs	=  fd-zio.o fd-spec.o fd-core.o
spec-fine-delay-objs	+= onewire.o spi.o i2c.o gpio.o
spec-fine-delay-objs	+= acam.o pll.o time.o

all: modules

modules_install clean modules:
	$(MAKE) -C $(LINUX) M=$(shell /bin/pwd) $@
