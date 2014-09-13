
BASE = mlvfs
EXEC = $(BASE).dll
CPP_OBJS = mlvfs-pfm.obj dng.obj index.obj
LIBS = advapi32.lib user32.lib

CPPFLAGS = /I.. /D_INTPTR=2 /O2

$(EXEC): $(CPP_OBJS)
	$(CXX) /Fe$(EXEC) /LD $(CPP_OBJS) $(LIBS)

dng.obj: ..\dng.c ..\dng.h ..\mlv.h ..\raw.h
	$(CXX) /c /TP $(CPPFLAGS) ..\dng.c

index.obj: ..\index.c ..\index.h ..\mlv.h ..\raw.h
	$(CXX) /c /TP $(CPPFLAGS) ..\index.c

install: $(EXEC)
	pfm register $(EXEC)

clean:
	del $(EXEC) $(BASE).exp $(BASE).lib $(CPP_OBJS)
