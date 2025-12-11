# mbdiscid - Disc ID calculator
# Makefile

# Detect platform
UNAME := $(shell uname -s)

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2

# Platform-specific settings
ifeq ($(UNAME),Darwin)
    PLATFORM = MACOS
    CFLAGS += -DPLATFORM_MACOS
    LDFLAGS = -framework CoreFoundation -framework IOKit -framework DiskArbitration
    LIBS = -ldiscid
    SCSI_SRC = scsi_macos.c
else
    PLATFORM = LINUX
    CFLAGS += -DPLATFORM_LINUX
    LDFLAGS =
    LIBS = -ldiscid
    SCSI_SRC = scsi_linux.c
endif

# Debug build
ifdef DEBUG
    CFLAGS += -g -O0 -DDEBUG
endif

# Version (use MBDISCID_VERSION to avoid conflict with IOKit headers)
MBDISCID_VERSION ?= 1.1.0b
CFLAGS += -DMBDISCID_VERSION=\"$(MBDISCID_VERSION)\"

# Source files (platform-specific SCSI)
SOURCES = main.c cli.c device.c toc.c discid.c output.c util.c $(SCSI_SRC) cdtext.c isrc.c
HEADERS = types.h cli.h device.h toc.h discid.h output.h util.h scsi.h cdtext.h isrc.h
OBJECTS = $(SOURCES:.c=.o)

# Target
TARGET = mbdiscid

# Default target
all: $(TARGET)

# Link
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS) $(LIBS)

# Compile
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

# Phony targets
.PHONY: all clean install uninstall test
