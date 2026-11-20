# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Jeringa
#
# Out-of-tree build for the MCT Trigger 5 DRM driver.
#
#   make            build trigger5.ko against the running kernel
#   make KDIR=...   build against another kernel's headers
#   make clean

obj-m := trigger5.o

KVERSION ?= $(shell uname -r)
KDIR     ?= /lib/modules/$(KVERSION)/build
PWD      := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all clean
