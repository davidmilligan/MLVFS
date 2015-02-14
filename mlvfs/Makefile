CC = gcc

ifeq "$(PLATFORM)" ""
PLATFORM := $(shell uname -s)
endif

DEBUG = -g

# CFLAGs and LIBS from `pkg-config fuse --cflags --libs`
ifeq "$(PLATFORM)" "Darwin"
INCLUDE = -I/usr/local/include/osxfuse
CFLAGS = -Wall -D__DARWIN_64_BIT_INO_T=1 -D__FreeBSD__=10 -D_FILE_OFFSET_BITS=64 $(INCLUDE) -std=gnu99
LIBS = -L/usr/local/lib/ -losxfuse -pthread
else
CFLAGS = -Wall -std=gnu99 -D_FILE_OFFSET_BITS=64
LIBS = -pthread -lfuse -lm
endif

EXEC = mlvfs
OBJS = dng.o index.o wav.o stripes.o cs.o amaze_demosaic_RT.o hdr.o histogram.o mongoose.o webgui.o resource_manager.o lj92.o

LZMA_DIR = LZMA/
LZMA_OBJS = $(LZMA_DIR)7zAlloc.o $(LZMA_DIR)7zBuf.o $(LZMA_DIR)7zBuf2.o $(LZMA_DIR)7zCrc.o $(LZMA_DIR)7zCrcOpt.o $(LZMA_DIR)7zDec.o $(LZMA_DIR)7zFile.o $(LZMA_DIR)7zIn.o $(LZMA_DIR)7zStream.o $(LZMA_DIR)Alloc.o $(LZMA_DIR)Bcj2.o $(LZMA_DIR)Bra.o $(LZMA_DIR)Bra86.o $(LZMA_DIR)BraIA64.o $(LZMA_DIR)CpuArch.o $(LZMA_DIR)Delta.o $(LZMA_DIR)LzFind.o $(LZMA_DIR)Lzma2Dec.o $(LZMA_DIR)Lzma2Enc.o $(LZMA_DIR)Lzma86Dec.o $(LZMA_DIR)Lzma86Enc.o $(LZMA_DIR)LzmaDec.o $(LZMA_DIR)LzmaEnc.o $(LZMA_DIR)LzmaLib.o $(LZMA_DIR)Ppmd7.o $(LZMA_DIR)Ppmd7Dec.o $(LZMA_DIR)Ppmd7Enc.o $(LZMA_DIR)Sha256.o $(LZMA_DIR)Xz.o $(LZMA_DIR)XzCrc64.o

default: $(EXEC)

debug: CFLAGS += $(DEBUG)
debug: $(EXEC)

$(EXEC): main.c $(OBJS) $(LZMA_OBJS)
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

%.o: %.c %.h
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f $(EXEC) $(OBJS) $(LZMA_OBJS)
