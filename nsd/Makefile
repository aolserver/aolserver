#
# The contents of this file are subject to the AOLserver Public License
# Version 1.1 (the "License"); you may not use this file except in
# compliance with the License. You may obtain a copy of the License at
# http://aolserver.com/.
#
# Software distributed under the License is distributed on an "AS IS"
# basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
# the License for the specific language governing rights and limitations
# under the License.
#
# The Original Code is AOLserver Code and related documentation
# distributed by AOL.
# 
# The Initial Developer of the Original Code is America Online,
# Inc. Portions created by AOL are Copyright (C) 1999 America Online,
# Inc. All Rights Reserved.
#
# Alternatively, the contents of this file may be used under the terms
# of the GNU General Public License (the "GPL"), in which case the
# provisions of GPL are applicable instead of those above.  If you wish
# to allow use of your version of this file only under the terms of the
# GPL and not to allow others to use your version of this file under the
# License, indicate your decision by deleting the provisions above and
# replace them with the notice and other provisions required by the GPL.
# If you do not delete the provisions above, a recipient may use your
# version of this file under either the License or the GPL.
# 
#
# $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Makefile,v 1.33 2002/03/03 16:14:43 jgdavidson Exp $
#

NSHOME ?= ..
buildnsd = 1
include $(NSHOME)/include/Makefile.global

LIB      =  libnsd.$(LIBEXT)
NSD	 =  nsd
INIT	 =  init.tcl

OBJS = \
	adpcmds.o adpeval.o adpparse.o adprequest.o auth.o binder.o	\
	cache.o callbacks.o cls.o compat.o cond.o config.o conn.o	\
	connio.o crypt.o dbdrv.o dbinit.o dbtcl.o dbutil.o dns.o	\
	driver.o dsprintf.o dstring.o encoding.o error.o exec.o		\
	fastpath.o fd.o filter.o fork.o form.o httptime.o index.o	\
	info.o lisp.o listen.o log.o master.o memory.o mimetypes.o	\
	modload.o nsconf.o nsmain.o nsthread.o op.o mutex.o cslock.o	\
	rwlock.o pathname.o pidfile.o proc.o queue.o quotehtml.o	\
	random.o reentrant.o request.o return.o rollfile.o sched.o	\
	sema.o server.o set.o signal.o sock.o sockcallback.o str.o	\
	tclatclose.o tclcmds.o tclconf.o tclenv.o tclfile.o tclhttp.o	\
	tclimg.o tclinit.o tcljob.o tclmisc.o tclobj.o tclrequest.o	\
	tclresp.o tclsched.o tclset.o tclshare.o tclsock.o tclthread.o	\
	tclvar.o tclxkeylist.o thread.o time.o tls.o unix.o url.o	\
	urlencode.o urlopen.o urlspace.o uuencode.o

all: $(LIB) $(NSD)

$(OBJS): nsd.h

$(LIB): $(OBJS)
	$(RM) $(LIB) stamp.o
	$(CC) -c -o stamp.o stamp.c
	$(LDLIB) -o $(LIB) $(OBJS) stamp.o $(LIBS) $(RPATH)

$(NSD): $(LIB) main.o
	$(RM) $(NSD)
	$(CC) -o $(NSD) main.o $(LIB) $(LIBS) $(RPATH)

install: all
	$(RM) $(INSTLIB)/$(LIB) $(INSTBIN)/$(NSD) $(INSTBIN)/$(INIT)
	$(CP) $(LIB) $(INSTLIB)/
	$(CP) $(NSD) $(INIT) $(INSTBIN)/

clean:
	$(RM) $(OBJS) $(LIB) $(NSD) main.o
