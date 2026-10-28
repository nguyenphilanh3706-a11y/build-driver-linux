obj-m += htn_sh1106_fb.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

dtbo:
	dtc -@ -I dts -O dtb -o htn-sh1106.dtbo htn-sh1106-overlay.dts

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f htn-sh1106.dtbo fb_test

fb_test: fb_test.c
	$(CC) -O2 -Wall -Wextra -o fb_test fb_test.c
