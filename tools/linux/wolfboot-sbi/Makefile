# Out-of-tree build of the wolfboot-sbi module against a kernel build tree:
#   make KDIR=/path/to/kernel/build ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu-
obj-m := wolfboot_sbi.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

modules_install:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules_install

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
