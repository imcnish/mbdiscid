# mbdiscid - Calculate disc IDs from CD or CDTOC data
# Copyright (c) 2025 Ian McNish
# SPDX-License-Identifier: MIT

CC ?= gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu99

# Platform-specific settings
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS - Homebrew paths + IOKit framework
    INCLUDES = -I/usr/local/include -I/opt/homebrew/include
    LIBS = -L/usr/local/lib -L/opt/homebrew/lib -ldiscid -framework IOKit -framework CoreFoundation
else ifeq ($(UNAME_S),Linux)
    # Linux - pkg-config if available, fallback to standard paths
    PKG_CONFIG := $(shell command -v pkg-config 2>/dev/null)
    ifdef PKG_CONFIG
        INCLUDES = $(shell pkg-config --cflags libdiscid 2>/dev/null)
        LIBS = $(shell pkg-config --libs libdiscid 2>/dev/null)
    endif
    ifndef LIBS
        INCLUDES = -I/usr/include -I/usr/local/include
        LIBS = -L/usr/lib -L/usr/local/lib -ldiscid
    endif
else
    INCLUDES = -I/usr/local/include
    LIBS = -L/usr/local/lib -ldiscid
endif

TARGET = mbdiscid
SOURCE = mbdiscid.c

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man1
MANPAGE = $(TARGET).1

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(TARGET) $(SOURCE) $(LIBS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/
	install -d $(DESTDIR)$(MANDIR)
	install -m 644 $(MANPAGE) $(DESTDIR)$(MANDIR)/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(MANDIR)/$(MANPAGE)
