# mbdiscid Makefile
# Supports Linux and macOS

PROG = mbdiscid
VERSION = 1.1.0

# Detect OS
UNAME := $(shell uname -s)

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -Werror -pedantic -std=c11 -O2
CFLAGS += -Wno-strict-prototypes
CFLAGS += -DVERSION=\"$(VERSION)\"
CFLAGS += -D_POSIX_C_SOURCE=200809L

# Debug build
ifdef DEBUG
CFLAGS += -g -O0 -DDEBUG
endif

# Platform-specific settings
ifeq ($(UNAME),Darwin)
    CFLAGS += -DPLATFORM_MACOS
    BREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo /usr/local)
    CFLAGS += -I$(BREW_PREFIX)/include
    LDFLAGS += -L$(BREW_PREFIX)/lib
    SCSI_OBJ = scsi_macos.o
else
    CFLAGS += -DPLATFORM_LINUX
    SCSI_OBJ = scsi_linux.o
endif

# Libraries - libdiscid detection
LIBDISCID_EXISTS := $(shell pkg-config --exists libdiscid 2>/dev/null && echo yes || echo no)
ifeq ($(LIBDISCID_EXISTS),yes)
    CFLAGS += $(shell pkg-config --cflags libdiscid)
    LDFLAGS += $(shell pkg-config --libs libdiscid)
    USE_REAL_DISCID := yes
else ifeq ($(UNAME),Darwin)
    LDFLAGS += -ldiscid
    USE_REAL_DISCID := yes
else
    CFLAGS += -DUSE_DISCID_STUB -I.
endif

# Object files
OBJECTS = cli.o device.o discid.o isrc.o main.o output.o toc.o util.o $(SCSI_OBJ)

# Add stub when not using real libdiscid
ifneq ($(USE_REAL_DISCID),yes)
    OBJECTS += discid_stub.o
endif

# Default target
all: $(PROG)

$(PROG): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(PROG) $(OBJECTS) $(LDFLAGS)

# Explicit compile rules
cli.o: cli.c cli.h types.h toc.h
	$(CC) $(CFLAGS) -c cli.c

device.o: device.c device.h types.h toc.h isrc.h util.h
	$(CC) $(CFLAGS) -c device.c

discid.o: discid.c discid.h toc.h util.h
	$(CC) $(CFLAGS) -c discid.c

discid_stub.o: discid_stub.c
	$(CC) $(CFLAGS) -c discid_stub.c

isrc.o: isrc.c isrc.h scsi.h types.h util.h
	$(CC) $(CFLAGS) -c isrc.c

main.o: main.c types.h cli.h toc.h discid.h device.h output.h util.h
	$(CC) $(CFLAGS) -c main.c

output.o: output.c output.h types.h toc.h discid.h util.h
	$(CC) $(CFLAGS) -c output.c

scsi_macos.o: scsi_macos.c scsi.h
	$(CC) $(CFLAGS) -c scsi_macos.c

scsi_linux.o: scsi_linux.c scsi.h
	$(CC) $(CFLAGS) -c scsi_linux.c

toc.o: toc.c toc.h types.h util.h
	$(CC) $(CFLAGS) -c toc.c

util.o: util.c util.h types.h
	$(CC) $(CFLAGS) -c util.c

# Clean
clean:
	rm -f $(PROG) *.o .deps

# Install
PREFIX ?= /usr/local
install: $(PROG)
	install -d $(PREFIX)/bin
	install -m 755 $(PROG) $(PREFIX)/bin/

# Test
test: $(PROG)
	./test.sh

.PHONY: all clean install test
