
NSHOME   =  ..

include $(NSHOME)/include/Makefile.global


LIBS	+= $(NSDLIBS)

OBJS = \
	adpparse.o binder.o dbinit.o fastpath.o lisp.o nsmain.o \
	request.o set.o tclenv.o tclsched.o tclshare.o adpcmds.o \
	adpeval.o adprequest.o auth.o cache.o callbacks.o cls.o config.o \
	conn.o crypt.o dbdrv.o dbtcl.o dbutil.o dns.o driver.o \
	dstring.o exec.o fd.o filter.o httptime.o htuu.o index.o info.o \
	keepalive.o listen.o log.o main.o mimetypes.o modload.o \
	nsconf.o nsthread.o op.o pathname.o pidfile.o proc.o queue.o \
	quotehtml.o random.o return.o rollfile.o sched.o server.o \
	sock.o sockcallback.o str.o tclcmds.o tclconf.o tclfile.o \
	tclinit.o tcljob.o tclmisc.o tclop.o tclresp.o tclset.o tclsock.o \
	tclthread.o tclxkeylist.o tclvar.o tls.o unix.o url.o \
	urlencode.o urlopen.o urlspace.o encoding.o

#
# Name the executables
#

INIT	 = init.tcl
NSD      = nsd
all: $(NSD)

$(NSD): $(LIBTCL) $(LIBNSTHREAD) $(OBJS) stamp.c
	$(CC) -c stamp.c
	$(CC) $(LDFLAGS) stamp.o $(OBJS) $(LIBTCL) $(LIBNSTHREAD) $(LIBS) -o $@

$(OBJS): nsd.h nsconf.h

install: $(NSD)
	$(RM) $(INSTBIN)/$(NSD)
	$(CP) $(NSD) $(INSTBIN)/
	$(CP) $(INIT) $(INSTBIN)/

clean:
	$(RM) $(NSD) $(OBJS) so_locations
