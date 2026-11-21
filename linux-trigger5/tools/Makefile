# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Jeringa
#
# Userspace probe and KMS test tool for the MCT Trigger 5 adapter.
#
#   make            build trigger5-tool
#   make clean

CC          ?= cc
PKG_CONFIG  ?= pkg-config

CFLAGS      ?= -O2 -g
CFLAGS      += -std=gnu11 -Wall -Wextra -Wno-unused-parameter
CFLAGS      += $(shell $(PKG_CONFIG) --cflags libdrm libusb-1.0)
LDLIBS      += $(shell $(PKG_CONFIG) --libs libdrm libusb-1.0) -lm

OBJS := trigger5-tool.o probe.o display.o pattern.o

all: trigger5-tool

trigger5-tool: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

trigger5-tool.o: trigger5-tool.c probe.h display.h pattern.h
probe.o: probe.c probe.h
display.o: display.c display.h
pattern.o: pattern.c pattern.h

clean:
	rm -f trigger5-tool $(OBJS)

.PHONY: all clean
