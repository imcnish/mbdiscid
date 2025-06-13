# Copyright (c) 2025 Ian McNish

CC = gcc
CFLAGS = -Wall -O2
INCLUDES = -I/usr/local/include
LIBS = -L/usr/local/lib -ldiscid
TARGET = mbdiscid
SOURCE = mbdiscid.c

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man1
MANPAGE = $(TARGET).1

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE) $(INCLUDES) $(LIBS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) $(BINDIR)/
	install -m 644 $(MANPAGE) $(MANDIR)/

uninstall:
	rm -f $(BINDIR)/$(TARGET)
	rm -f $(MANDIR)/$(MANPAGE)

.PHONY: all clean install uninstall
