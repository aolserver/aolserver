NSHOME          = ..\..
ROOT            = $(NSHOME)\nsd

!INCLUDE $(NSHOME)/include/Makefile.win32

NSD7            = $(TMPDIR)\nsd76.exe
NSD8            = $(TMPDIR)\nsd8x.exe

OBJS =  $(TMPDIR)\main.obj

LIBS =  $(NSHOME)/win32/nsd/Release/nsd8x.lib

######################################################################
# Project specific targets
######################################################################

all:        setup $(NSD8)

$(NSD8): $(OBJS) $(LIBS)
    $(link32) $(ldebug) $(conlflags) \
        -out:$@ $(conlibsdll) @<<
        $(LIBNSTHREAD) $(LIBTCL8X) $(LIBS)
        $(OBJS)
<<

