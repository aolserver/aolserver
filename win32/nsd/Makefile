NSHOME          = ..\..
ROOT            = $(NSHOME)\nsd

!INCLUDE $(NSHOME)/include/Makefile.win32

NSD7DLL         = $(TMPDIR)\nsd76.dll
NSD8DLL         = $(TMPDIR)\nsd8x.dll

OBJS = \
        $(TMPDIR)\adp.obj \
        $(TMPDIR)\adpfancy.obj \
        $(TMPDIR)\auth.obj \
        $(TMPDIR)\cache.obj \
        $(TMPDIR)\callbacks.obj \
        $(TMPDIR)\config.obj \
        $(TMPDIR)\conn.obj \
        $(TMPDIR)\crypt.obj \
        $(TMPDIR)\dbdrv.obj \
        $(TMPDIR)\dbinit.obj \
        $(TMPDIR)\dbtcl.obj \
        $(TMPDIR)\dbutil.obj \
        $(TMPDIR)\dns.obj \
        $(TMPDIR)\drv.obj \
        $(TMPDIR)\dstring.obj \
        $(TMPDIR)\exec.obj \
        $(TMPDIR)\fastpath.obj \
        $(TMPDIR)\fd.obj \
        $(TMPDIR)\filter.obj \
        $(TMPDIR)\getopt.obj \
        $(TMPDIR)\httptime.obj \
        $(TMPDIR)\htuu.obj \
        $(TMPDIR)\index.obj \
        $(TMPDIR)\keepalive.obj \
        $(TMPDIR)\lisp.obj \
        $(TMPDIR)\listen.obj \
        $(TMPDIR)\log.obj \
        $(TMPDIR)\mimetypes.obj \
        $(TMPDIR)\modload.obj \
        $(TMPDIR)\nsconf.obj \
        $(TMPDIR)\nsmain.obj \
        $(TMPDIR)\nsthread.obj \
        $(TMPDIR)\nswin32.obj \
        $(TMPDIR)\op.obj \
        $(TMPDIR)\pathname.obj \
        $(TMPDIR)\pidfile.obj \
        $(TMPDIR)\proc.obj \
        $(TMPDIR)\quotehtml.obj \
        $(TMPDIR)\random.obj \
        $(TMPDIR)\request.obj \
        $(TMPDIR)\return.obj \
        $(TMPDIR)\rollfile.obj \
        $(TMPDIR)\sched.obj \
        $(TMPDIR)\serv.obj \
        $(TMPDIR)\set.obj \
        $(TMPDIR)\sock.obj \
        $(TMPDIR)\sockcallback.obj \
        $(TMPDIR)\str.obj \
        $(TMPDIR)\tclcmds.obj \
        $(TMPDIR)\tclconf.obj \
        $(TMPDIR)\tclenv.obj \
        $(TMPDIR)\tclfile.obj \
        $(TMPDIR)\tclinit.obj \
        $(TMPDIR)\tclmisc.obj \
        $(TMPDIR)\tclatclose.obj \
        $(TMPDIR)\tclrequest.obj \
        $(TMPDIR)\tclresp.obj \
        $(TMPDIR)\tclsched.obj \
        $(TMPDIR)\tclset.obj \
        $(TMPDIR)\tclsock.obj \
        $(TMPDIR)\tclstats.obj \
        $(TMPDIR)\tclthread.obj \
        $(TMPDIR)\tclvar.obj \
        $(TMPDIR)\tclxkeylist.obj \
        $(TMPDIR)\url.obj \
        $(TMPDIR)\urlencode.obj \
        $(TMPDIR)\urlopen.obj \
        $(TMPDIR)\urlspace.obj

TCL76STUB       = $(TMPDIR)\tclstub76.obj
TCL8XSTUB       = $(TMPDIR)\tclstub8x.obj

TCL_CFLAGS      = /D "NSD_EXPORTS" $(TCL_CFLAGS)

######################################################################
# Project specific targets
######################################################################

all:        setup $(NSD8DLL)

$(NSD8DLL): $(TCL8XSTUB) $(OBJS) $(ROOT)\stamp.c
    $(cc32) $(TCL_CFLAGS) -Fo$(TMPDIR)\ $(ROOT)\stamp.c
    $(link32) $(ldebug) $(dlllflags) \
        -out:$@ $(guilibsdll) @<<
        $(TCL8XSTUB) $(TMPDIR)\stamp.obj
        $(LIBNSTHREAD) $(LIBTCL8X)
        $(OBJS) 
<<

$(TCL76STUB) $(TCL8XSTUB): $(ROOT)\tclstubs.cpp $(ROOT)\tclnsshare.cpp \
                           $(ROOT)\tclinvoke.cpp


