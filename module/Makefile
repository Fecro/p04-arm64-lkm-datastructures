# Out-of-tree Kbuild Makefile for composite kds_store LKM

obj-m += kds_store.o
kds_store-y := kds_main.o kds_core.o kds_procfs.o kds_sysfs.o

ARCH          ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-
KDIR          ?= $(CURDIR)/../build/kernel
PWD           := $(shell pwd)

.PHONY: all clean

all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
