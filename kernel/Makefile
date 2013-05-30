
LINUX ?= /lib/modules/$(shell uname -r)/build
ZIO ?= $(M)/../zio
FMC_BUS ?= $(M)/../fmc-bus

KBUILD_EXTRA_SYMBOLS := \
	$(ZIO)/Module.symvers \
	$(FMC_BUS)/kernel/Module.symvers

GIT_VERSION = $(shell cd $(src); git describe --dirty --long --tags)

ccflags-y = \
	-I$(ZIO)/include \
	-I$(FMC_BUS)/kernel/include \
	-I$(FMC_BUS)/kernel/include/linux \
	-I$M

ccflags-y += -DGIT_VERSION=\"$(GIT_VERSION)\"
#ccflags-y += -DDEBUG

subdirs-ccflags-y = $(ccflags-y)

obj-m := fmc-fine-delay.o

fmc-fine-delay-objs	=  fd-zio.o fd-irq.o fd-core.o
fmc-fine-delay-objs	+= onewire.o spi.o i2c.o gpio.o
fmc-fine-delay-objs	+= acam.o calibrate.o pll.o time.o
fmc-fine-delay-objs	+= calibration.o
fmc-fine-delay-objs	+= ../sdb-lib/access.o
fmc-fine-delay-objs	+= ../sdb-lib/glue.o

all modules:
	$(MAKE) -C $(LINUX) M=$(shell /bin/pwd) modules

install modules_install:
	$(MAKE) -C $(LINUX) M=$(shell /bin/pwd) modules_install

# be able to run the "clean" rule even if $(LINUX) is not valid
clean:
	rm -rf *.o *~  .*.cmd *.ko *.mod.c .tmp_versions Module.symvers \
		Module.markers modules.order
