CC = gcc

ifeq "$(PLATFORM)" ""
PLATFORM := $(shell uname -s)
endif

# CFLAGs and LIBS from `pkg-config fuse --cflags --libs`
ifeq "$(PLATFORM)" "Darwin"
INCLUDE = -I/usr/local/include/osxfuse
CFLAGS = -D__DARWIN_64_BIT_INO_T=1 -D__FreeBSD__=10 -D_FILE_OFFSET_BITS=64 $(INCLUDE)
LIBS = -L/usr/local/lib/ -losxfuse -pthread
else
CFLAGS = -std=gnu99 -D_FILE_OFFSET_BITS=64
LIBS = -pthread -lfuse
endif

EXEC = mlvfs
OBJS = dng.o

default: $(EXEC)

$(EXEC): main.c $(OBJS)
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

%.o: %.c %.h
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f $(EXEC) $(OBJS)
