TARGET          = nsperm

NSHOME          = ..\..
ROOT            = $(NSHOME)\$(TARGET)

!INCLUDE $(NSHOME)\include\Makefile.win32

OBJS    = $(TMPDIR)\$(TARGET).obj

TCL_CFLAGS      = /D "NSPERM_EXPORTS" $(TCL_CFLAGS)

######################################################################
# Project specific targets
######################################################################

all:	setup $(TMPDIR)\$(TARGET).dll

$(TMPDIR)\$(TARGET).dll: $(OBJS)
    $(link32) $(ldebug) $(dlllflags) \
        -out:$@ $(guilibsdll) @<<
        $(LIBNSTHREAD) $(LIBTCL8X) $(LIBNSD8X)
        $(OBJS)
<<


