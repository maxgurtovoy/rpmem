# Define a common prefix where binaries and docs install
PREFIX = /usr
sbindir = bin

CC = gcc
CFLAGS += -g -ggdb -Wall -W -D_GNU_SOURCE
LDFLAGS = -libverbs -lpthread -lrdmacm

OBJECTS_LAT = simple_open_close.o rpmem.o
TARGETS = simple_open_close

all: $(TARGETS)

simple_open_close: simple_open_close.o rpmem.o
	$(CC) $(CFLAGS) $(LDFLAGS) simple_open_close.o rpmem.o -o $@

install:
	install -d -m 755 $(PREFIX)/$(sbindir)
	install -m 755 $(TARGETS) $(PREFIX)/$(sbindir)
clean:
	rm -f $(OBJECTS_LAT) $(TARGETS)
