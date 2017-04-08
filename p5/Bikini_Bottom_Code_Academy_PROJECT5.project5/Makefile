# Makefile for JProbe Kernel Modules
#
#

obj-m	:= jprobe_p5final.o
KDIR	:= /lib/modules/$(shell uname -r)/build

PWD	:= $(shell pwd)

all: jprobe_p5final.c
	make -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	make -C $(KDIR) SUBDIRS=$(PWD) clean
