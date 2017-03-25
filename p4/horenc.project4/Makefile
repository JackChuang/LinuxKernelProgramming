#
# Makefile
# jackchuang, 2017-03-17 21:10
#

#KERNELDIR ?= /lib/modules/4.0.9/build
KERNELDIR ?= /lib/modules/$(shell uname -r)/build

all: example.c
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean: example.c
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions Module.symvers modules.order 

obj-m += example.o

# vim:ft=make
#
