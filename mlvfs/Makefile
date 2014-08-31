CC = gcc

CFLAGS = -std=gnu99 -D_FILE_OFFSET_BITS=64
LIBS = -lfuse

EXEC = mlvfs
OBJS = dng.o

default: $(EXEC)

$(EXEC): main.c $(OBJS)
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

%.o: %.c %.h
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f $(EXEC) $(OBJS)
