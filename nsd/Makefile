
NSHOME   =  ..

include $(NSHOME)/include/Makefile.global


LIBS	+= $(NSDLIBS)

OBJS = \
	adpparse.c adpcmds.c adpeval.c adprequest.c
	info.o
        auth.o binder.o cache.o callbacks.o config.o 
        conn.o crypt.o dbdrv.o dbinit.o dbtcl.o dbutil.o dns.o driver.o 
        dstring.o exec.o fastpath.o fd.o filter.o httptime.o htuu.o 
        index.o keepalive.o lisp.o listen.o log.o main.o 
        mimetypes.o modload.o nsconf.o nsmain.o 
        nsthread.o op.o pathname.o pidfile.o proc.o 
        queue.o set.o sock.o sockcallback.o str.o 
        quotehtml.o random.o request.o return.o rollfile.o sched.o 
        tclcmds.o tclconf.o tclenv.o tclfile.o tclinit.o tclmisc.o 
        tclop.o tclresp.o tclsched.o tclset.o tclsock.o 
        tclxkeylist.o tclthread.o tclvar.o unix.o url.o 
        urlencode.o urlopen.o urlspace.o tclshare.o server.o nstls.o 



#
# Name the executables
#

NSD      = nsd
all: $(NSD)

$(NSD): $(LIBTCL) ../thread/libnsthread.so $(OBJS) stamp.c
	$(CC) -c stamp.c
	$(CC) $(LDFLAGS) stamp.o $(OBJS) $(LIBTCL) -L../thread -lnsthread $(LIBS) -o $@

$(OBJS): nsd.h nsconf.h

install: $(NSD)
	$(RM) $(INSTBIN)/$(NSD)
	$(CP) $(NSD) $(INSTBIN)/

clean:
	$(RM) $(NSD) $(OBJS) so_locations
