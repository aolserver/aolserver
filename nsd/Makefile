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
# $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Makefile,v 1.11.2.2.2.2 2002/09/23 23:06:47 jgdavidson Exp $
#

INIT	= init.tcl
LIB     = nsd
PGM	= nsd
PGMOBJS	= main.o
HDRS	= nsd.h

LOBJS = \
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
        urlencode.o urlopen.o urlspace.o tclstub8x.o

LIBOBJS=$(LOBJS) stamp.o

include ../include/Makefile.build

stamp.o: $(LOBJS)
	$(CC) -c -o stamp.o stamp.c

tclstub8x.o: tclnsshare.cpp tclinvoke.cpp tclstubs.cpp

install-init:
	$(RM) $(INSTBIN)/$(INIT)
	$(CP) $(INIT) $(INSTBIN)/
