
NSHOME   =  ..

include $(NSHOME)/include/Makefile.global


LIBS	+= $(NSDLIBS)

OBJS = \
	adpcmds.o adpeval.o adpparse.o adprequest.o auth.o binder.o \
	cache.o callbacks.o cls.o config.o conn.o connio.o crypt.o \
	dbdrv.o dbinit.o dbtcl.o dbutil.o dns.o driver.o dstring.o \
	encoding.o exec.o fastpath.o fd.o filter.o form.o \
	httptime.o index.o info.o lisp.o listen.o log.o \
	main.o mimetypes.o modload.o nsconf.o nsmain.o nsthread.o \
	op.o pathname.o pidfile.o proc.o queue.o quotehtml.o \
	random.o request.o return.o rollfile.o sched.o server.o \
	set.o sock.o sockcallback.o str.o tclatclose.o tclcmds.o tclconf.o \
	tclenv.o tclfile.o tclhttp.o tclimg.o tclinit.o tcljob.o tclmisc.o \
	tclresp.o tclrequest.o tclsched.o tclset.o tclshare.o tclsock.o \
	tclthread.o tclvar.o tclxkeylist.o tls.o unix.o url.o \
	urlencode.o urlopen.o urlspace.o uuencode.o

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
