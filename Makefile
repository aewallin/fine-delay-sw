
LINUX ?= /lib/modules/$(shell uname -r)/build
ZIO ?= $(HOME)/zio
SPEC_SW ?= $(HOME)/spec-sw
FMC_BUS ?= $(HOME)/fmc-bus

KBUILD_EXTRA_SYMBOLS := \
	$(ZIO)/Module.symvers \
	$(SPEC_SW)/kernel/Module.symvers \
	$(FMC_BUS)/kernel/Module.sysmvers


ccflags-y = \
	-I$(ZIO)/include \
	-I$(SPEC_SW)/kernel \
	-I$(SPEC_SW)/kernel/include \
	-I$(FMC_BUS)/kernel/include \
	-I$M

#ccflags-y += -DDEBUG

subdirs-ccflags-y = $(ccflags-y)

obj-m := spec-fine-delay.o

spec-fine-delay-objs	=  fd-zio.o fd-core.o
spec-fine-delay-objs	+= onewire.o spi.o i2c.o gpio.o
spec-fine-delay-objs	+= acam.o calibrate.o pll.o time.o

all: modules

modules_install clean modules:
	$(MAKE) -C $(LINUX) M=$(shell /bin/pwd) $@
	$(MAKE) -C tools  M=$(shell /bin/pwd) $@
	$(MAKE) -C lib $@
