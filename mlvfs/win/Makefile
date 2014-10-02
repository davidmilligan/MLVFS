
BASE = mlvfs
EXEC = $(BASE).dll
CPP_OBJS = mlvfs-pfm.obj files.obj dng.obj histogram.obj hdr.obj index.obj kelvin.obj wav.obj
RES = mlvfs.res
LIBS = advapi32.lib user32.lib

CPPFLAGS = /I.. /D_INTPTR=2 /O2 /W2 /nologo
RFLAGS = /r /nologo

$(EXEC): $(CPP_OBJS) $(RES)
	$(CXX) /Fe$(EXEC) /LD /nologo $(CPP_OBJS) $(RES) $(LIBS)

!if [git --version > nul 2>&1] == 0
mlvfs.res: mlvfs.rc
	for /f %x in ('git rev-parse --verify --short HEAD') do rc /d "GIT_COMMIT=\"%x\"" $(RFLAGS) $**
!endif

{..}.c{}.obj::
	$(CXX) /c /TP $(CPPFLAGS) $<

install: $(EXEC)
	pfm register $(EXEC)

clean:
	del $(EXEC) $(BASE).exp $(BASE).lib $(CPP_OBJS) $(RES)
