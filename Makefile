# Define a common prefix where binaries and docs install
PREFIX = /usr
sbindir = bin

CC = gcc
CFLAGS += -g -ggdb -Wall -W -D_GNU_SOURCE
LDFLAGS = -libverbs -lpthread -lrdmacm

OBJECTS_LAT = rpmem_server.o simple_open_close.o simple_map_unmap.o simple_commit.o rpmem.o rpmem_common.o rpmem_protocol.o rpmem_log.o
TARGETS = rpmem_server simple_open_close simple_map_unmap simple_commit

all: $(TARGETS)

rpmem_server: rpmem_server.o rpmem_common.o rpmem_protocol.o rpmem_log.o
	$(CC) $(CFLAGS) $(LDFLAGS) rpmem_server.o rpmem_common.o rpmem_protocol.o rpmem_log.o -o $@

simple_open_close: simple_open_close.o rpmem.o rpmem_common.o rpmem_protocol.o rpmem_log.o
	$(CC) $(CFLAGS) $(LDFLAGS) simple_open_close.o rpmem.o rpmem_common.o rpmem_protocol.o rpmem_log.o -o $@

simple_map_unmap: simple_map_unmap.o rpmem.o rpmem_common.o rpmem_protocol.o rpmem_log.o
	$(CC) $(CFLAGS) $(LDFLAGS) simple_map_unmap.o rpmem.o rpmem_common.o rpmem_protocol.o rpmem_log.o -o $@

simple_commit: simple_commit.o rpmem.o rpmem_common.o rpmem_protocol.o rpmem_log.o
	$(CC) $(CFLAGS) $(LDFLAGS) simple_commit.o rpmem.o rpmem_common.o rpmem_protocol.o rpmem_log.o -o $@

install:
	install -d -m 755 $(PREFIX)/$(sbindir)
	install -m 755 $(TARGETS) $(PREFIX)/$(sbindir)
clean:
	rm -f $(OBJECTS_LAT) $(TARGETS)
