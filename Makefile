# mbdiscid - Disc ID calculator
# Copyright (C) 2025 Ian McNish
# SPDX-License-Identifier: GPL-3.0-or-later
# Makefile

# Detect platform
UNAME := $(shell uname -s)

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -D_POSIX_C_SOURCE=200809L

# Platform-specific settings
ifeq ($(UNAME),Darwin)
    CFLAGS += -DPLATFORM_MACOS
    LDFLAGS = -framework CoreFoundation -framework IOKit -framework DiskArbitration
    LIBS = -ldiscid
    SCSI_SRC = scsi_macos.c
else
    CFLAGS += -DPLATFORM_LINUX
    LDFLAGS =
    LIBS = -ldiscid
    SCSI_SRC = scsi_linux.c
endif

# Debug build
ifdef DEBUG
    CFLAGS += -g -O0 -DDEBUG
endif

# Version
MBDISCID_VERSION ?= 1.1.0d
CFLAGS += -DMBDISCID_VERSION=\"$(MBDISCID_VERSION)\"

# Source and header files
SCSI_EXCLUDE = scsi_macos.c scsi_linux.c
SOURCES = $(filter-out $(SCSI_EXCLUDE), $(wildcard *.c)) $(SCSI_SRC)
HEADERS = $(wildcard *.h)
OBJECTS = $(SOURCES:.c=.o)

# Target
TARGET = mbdiscid

# Default target
all: $(TARGET)

# Link
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS) $(LIBS)

# Compile - rebuild if any header changes
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean
clean:
	rm -f *.o $(TARGET)

# Install
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

# Test
test: $(TARGET)
	./test.sh

.PHONY: all clean install uninstall test
