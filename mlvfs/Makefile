CC = gcc

CFLAGS = -std=gnu99 -D_FILE_OFFSET_BITS=64
INCLUDE = -I/usr/local/include/osxfuse
LIBS = -L/usr/local/lib/ -lfuse

EXEC = mlvfs
OBJS = dng.o

default: $(EXEC)

$(EXEC): main.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDE) $^ $(LIBS) -o $@

%.o: %.c %.h
	$(CC) -c $(CFLAGS) $(INCLUDE) $< -o $@

clean:
	rm -f $(EXEC) $(OBJS)
