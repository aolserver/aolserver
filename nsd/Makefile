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
# $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Makefile,v 1.44 2005/03/25 00:32:14 jgdavidson Exp $
#

INSTALL	= install-init
LIB	= nsd
PGM	= nsd
PGMOBJS	= main.o
LIBINIT	= NsdInit
HDRS	= nsd.h

LOBJS	= adpcmds.o adpeval.o adpparse.o adprequest.o auth.o binder.o \
	  cache.o callbacks.o cls.o compress.o config.o conn.o connio.o \
	  crypt.o dns.o driver.o dsprintf.o dstring.o encoding.o exec.o \
	  fastpath.o fd.o filter.o form.o httptime.o index.o info.o \
	  init.o limits.o lisp.o listen.o log.o mimetypes.o modload.o \
	  nsconf.o nsmain.o nsthread.o op.o pathname.o pidfile.o pools.o \
	  proc.o queue.o quotehtml.o random.o request.o return.o \
	  rollfile.o sched.o server.o set.o sock.o sockcallback.o str.o \
	  task.o tclcmds.o tclconf.o tclenv.o tclfile.o \
	  tclhttp.o tclimg.o tclinit.o tcljob.o tclmisc.o tclnhttp.o \
	  tclobj.o tclrequest.o tclresp.o tclsched.o tclset.o tclshare.o \
	  tclsock.o tclthread.o tclvar.o tclxkeylist.o unix.o url.o \
	  urlencode.o urlopen.o urlspace.o uuencode.o

LIBOBJS	= $(LOBJS) stamp.o

include ../include/Makefile.build

stamp.o: $(LOBJS)
	$(CC) -c -o stamp.o stamp.c

install-init:
	$(INSTALL_DATA) init.tcl $(INSTBIN)/
