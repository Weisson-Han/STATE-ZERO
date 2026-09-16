obj-m := STATE-ZERO.o

KERNEL_DIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

ccflags-y := -DDEBUG -g

all: modules

modules:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	rm -f *.test

test-deps:
	@which sparse > /dev/null || echo "Install sparse for static analysis"

sparse: test-deps
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) C=2 CF="-D__CHECK_ENDIAN__" modules

checkpatch:
	$(KERNEL_DIR)/scripts/checkpatch.pl --strict --file *.c *.h

gcov:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) GCOV_PROFILE=y modules

tags:
	ctags -R --c-kinds=+p --fields=+iaS --extra=+q .

cscope:
	cscope -Rbq

test: modules
	@echo "Loading module..."
	@sudo insmod $(PWD)/STATE-ZERO.ko
	@echo "Running basic test..."
	@sleep 1
	@dmesg | grep "STATE-ZERO" | tail -5
	@echo "Unloading module..."
	@sudo rmmod STATE-ZERO 2>/dev/null || true

perf:
	perf record -a -g -- insmod $(PWD)/my_driver.ko
	perf report

debug:
	echo ttyS0 > /sys/module/kgdboc/parameters/kgdboc
	echo g > /proc/sysrq-trigger

kmemleak:
	echo scan > /sys/kernel/debug/kmemleak
	cat /sys/kernel/debug/kmemleak

sign:
	$(KERNEL_DIR)/scripts/sign-file sha256 \
		$(KERNEL_DIR)/certs/signing_key.pem \
		$(KERNEL_DIR)/certs/signing_key.x509 \
		my_driver.ko

.PHONY: all modules clean test sparse checkpatch tags cscope

