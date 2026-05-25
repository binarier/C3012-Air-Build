PWD := $(shell pwd)

ccflags-y += -I$(src)

obj-m += gpio-counter.o

all:
	$(MAKE) ARCH=$(ARCH) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	rm -f *.o* *.ko
	rm -f Module.symvers modules.order
	rm -rf .tmp_versions
	rm -f .*cmd
	rm -f *.mod *.mod.c
