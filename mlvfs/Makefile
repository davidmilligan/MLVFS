CC = gcc

ifeq "$(PLATFORM)" ""
PLATFORM := $(shell uname -s)
endif

DEBUG = -g

# CFLAGs and LIBS from `pkg-config fuse --cflags --libs`
ifeq "$(PLATFORM)" "Darwin"
INCLUDE = -I/usr/local/include/osxfuse
CFLAGS = -Wall -D__DARWIN_64_BIT_INO_T=1 -D__FreeBSD__=10 -D_FILE_OFFSET_BITS=64 $(INCLUDE)
LIBS = -L/usr/local/lib/ -losxfuse -pthread
else
CFLAGS = -Wall -std=gnu99 -D_FILE_OFFSET_BITS=64
LIBS = -pthread -lfuse -lm
endif

EXEC = mlvfs
OBJS = dng.o index.o wav.o kelvin.o stripes.o cs.o hdr.o histogram.o

default: $(EXEC)

debug: CFLAGS += $(DEBUG)
debug: $(EXEC)

$(EXEC): main.c $(OBJS)
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

%.o: %.c %.h
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f $(EXEC) $(OBJS)
