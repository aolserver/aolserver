
NSHOME   =  ..

include $(NSHOME)/include/Makefile.global


LIBS	+= $(NSDLIBS)

OBJS = \
        adp.o adpfancy.o auth.o binder.o cache.o callbacks.o config.o \
        conn.o crypt.o dbdrv.o dbinit.o dbtcl.o dbutil.o dns.o drv.o \
        dstring.o exec.o fastpath.o fd.o filter.o httptime.o htuu.o \
        id.o index.o keepalive.o lisp.o listen.o log.o main.o \
        mimetypes.o modload.o nsconf.o nsmain.o \
        nsthread.o op.o pathname.o pidfile.o proc.o \
        quotehtml.o random.o request.o return.o rollfile.o sched.o \
        serv.o set.o sock.o sockcallback.o str.o \
        tclcmds.o tclconf.o tclenv.o tclfile.o tclinit.o tclmisc.o \
        tclop.o tclresp.o tclsched.o tclset.o tclsock.o tclstats.o \
        tclxkeylist.o tclthread.o tclvar.o unix.o url.o \
        urlencode.o urlopen.o urlspace.o

TCL76STUB = tclstub76.o
TCL8XSTUB = tclstub8x.o


#
# Name the executables
#
NSD7      = nsd76
NSD8      = nsd8x


all: $(NSD7) $(NSD8)

#
# Tcl 7.6:  Our hacked version of Tcl 7.6 is fast and robust.
#
$(NSD7): libtcl76 libnsthread $(TCL76STUB) $(OBJS) stamp.c
	$(CC) -c stamp.c
	$(CC) $(LDFLAGS) $(TCL76STUB) stamp.o $(OBJS) $(LIBTCL76) $(LIBNSTHREAD) $(LIBS) -o $@


#
# Tcl 8.x:  Slower but at least it's Tcl 8.x.
#
$(NSD8): libtcl8x libnsthread $(TCL8XSTUB) $(OBJS) stamp.c
	$(CC) -c stamp.c
	$(CC) $(LDFLAGS) $(TCL8XSTUB) stamp.o $(OBJS) $(LIBTCL8X) $(LIBNSTHREAD) $(LIBS) -o $@


#
# Tcl-dependent objects
#
$(TCL76STUB) $(TCL8XSTUB): tclstubs.cpp tclnsshare.cpp tclinvoke.cpp


#
# Installation
#
#  Note: The modern Tcl 8.x nsd is known as "nsd8x", not "nsd82" or "nsd83".
#        The older Tcl 7.6 nsd will remain as "nsd76".
#
install: $(NSD7) $(NSD8)
	$(RM) $(INSTBIN)/nsd82 $(INSTBIN)/nsd
	$(RM) $(INSTBIN)/$(NSD7) $(INSTBIN)/$(NSD8)
	$(CP) $(NSD7) $(NSD8) $(INSTBIN)
	cd $(INSTBIN) ; $(RM) nsd ; $(LN) ./$(NSD7) nsd

#
# Clean the directory to pre-built state.
#
clean:
	$(RM) $(NSD7) $(NSD8) $(OBJS) $(TCL76STUB) $(TCL8XSTUB) so_locations

clobber: clean
	$(RM) *.so *.o *.a *~

distclean: clobber
	$(RM) TAGS core
