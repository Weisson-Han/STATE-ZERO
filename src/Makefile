# SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only

obj-m := state-zero.o

state-zero-y := main.o bio_redirection.o globals.o

KERNEL_DIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

ccflags-y := -DDEBUG -g -I$(src)/../includes/

all: modules

modules:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	rm -f *.test

