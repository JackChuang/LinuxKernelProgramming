obj-m += prototype.o
prototype-objs := core.o device.o

# Kernel source root directory :
KERN_DIR=~/Courses/LKP/Project6/VM/linux-4.0.9
# Target architecture :
ARCH=x86_64

TARGET=root@10.1.1.169:~

all:
	make -C $(KERN_DIR) M=$(PWD) ARCH=$(ARCH) modules
	# cleanup
	rm -rf *.o *.mod.c modules.order Module.symvers

install: all
	scp -r prototype.ko insert_mod.sh  $(TARGET)
	
clean:
	make -C $(KERN_DIR) M=$(PWD) ARCH=$(ARCH) clean
