
all: kernel_mod mkfs.vvsfs truncate view.vvsfs hello

mkfs.vvsfs: mkfs.vvsfs.c
	gcc -Wall -o $@ $<

truncate: truncate.c
	gcc -Wall -o $@ $<

view.vvsfs: view.vvsfs.c
	gcc -Wall -o $@ $<

hello: hello.c
	gcc -s -Wl,-z,norelro -Wl,--hash-style=gnu -Wl,--build-id=none -ffunction-sections -fdata-sections -Wl,--gc-sections -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-math-errno -fno-ident -fno-stack-protector -Wall -Os -o $@ $<

ifneq ($(KERNELRELEASE),)
# kbuild part of makefile, for backwards compatibility
include Kbuild

else
# normal makefile
KDIR ?= /usr/src/linux-headers-`uname -r`

kernel_mod:
	$(MAKE) -C $(KDIR) M=$$PWD

endif
