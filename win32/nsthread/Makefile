NSHOME          = ..\..
ROOT            = $(NSHOME)\thread

!INCLUDE $(NSHOME)/include/Makefile.win32

OBJS    = \
          $(TMPDIR)\compat.obj \
          $(TMPDIR)\cond.obj \
          $(TMPDIR)\cs.obj \
          $(TMPDIR)\error.obj \
          $(TMPDIR)\memory.obj \
          $(TMPDIR)\mutex.obj \
          $(TMPDIR)\osthread.obj \
          $(TMPDIR)\pool.obj \
          $(TMPDIR)\reentrant.obj \
          $(TMPDIR)\rwlock.obj \
          $(TMPDIR)\sema.obj \
          $(TMPDIR)\tcl8x.obj \
          $(TMPDIR)\thread.obj \
          $(TMPDIR)\time.obj \
          $(TMPDIR)\tls.obj \
          $(TMPDIR)\debug.obj

LIBNSTHREAD     = $(TMPDIR)\nsthread.dll

TCL_CFLAGS      = /D "NSTHREAD_EXPORTS" $(TCL_CFLAGS)

######################################################################
# Project specific targets
######################################################################

all:    setup $(LIBNSTHREAD)

$(LIBNSTHREAD): $(OBJS)
    $(link32) $(ldebug) $(dlllflags) \
        -out:$@ $(guilibsdll) @<<
$(OBJS)
<<

$(TMPDIR)\osthread.obj: $(ROOT)\win32.cpp

$(OBJS): $(ROOT)\thread.h

