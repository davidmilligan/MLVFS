
BASE = mlvfs
EXEC = $(BASE).dll
CPP_OBJS = mlvfs-pfm.obj dng.obj index.obj kelvin.obj wav.obj
RES = mlvfs.res
LIBS = advapi32.lib user32.lib

CPPFLAGS = /I.. /D_INTPTR=2 /O2

$(EXEC): $(CPP_OBJS) $(RES)
	$(CXX) /Fe$(EXEC) /LD $(CPP_OBJS) $(RES) $(LIBS)

!if [git --version > nul 2>&1] == 0
mlvfs.res: mlvfs.rc
	for /f %x in ('git rev-parse --verify --short HEAD') do rc /d "GIT_COMMIT=\"%x\"" /r $**
!endif

dng.obj: ..\dng.c ..\dng.h ..\mlv.h ..\raw.h ..\kelvin.h
	$(CXX) /c /TP $(CPPFLAGS) ..\dng.c

kelvin.obj: ..\kelvin.c ..\kelvin.h
	$(CXX) /c /TP $(CPPFLAGS) ..\kelvin.c

index.obj: ..\index.c ..\index.h ..\mlv.h ..\raw.h
	$(CXX) /c /TP $(CPPFLAGS) ..\index.c

wav.obj: ..\wav.c ..\wav.h ..\index.h ..\mlv.h ..\raw.h
	$(CXX) /c /TP $(CPPFLAGS) ..\wav.c

install: $(EXEC)
	pfm register $(EXEC)

clean:
	del $(EXEC) $(BASE).exp $(BASE).lib $(CPP_OBJS) $(RES)
