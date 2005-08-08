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
# $Header: /Users/dossy/Desktop/cvs/aolserver/examples/mynsd/Makefile,v 1.1 2005/08/08 12:14:30 jgdavidson Exp $
#

# 
# Example simple Makefile to build a statically linked nsd.  In addition
# to building all the AOLserver libraries and code, Tcl must first be
# built statically as well.  This makefile is only known to work on Linux.
# 
# Once built, you should verify all symbols are in "mynsd" with nm and
# that no AOLserver or Tcl dynamic libraries will be used with "ldd".
# And be sure to comment out the cooresponding ns/server/{server}/modules
# section as the server will still attempt to load those modules which
# will generally confuse things.
# 

AOLSERVER ?= /usr/local/aolserver
LIB = $(AOLSERVER)/lib
BIN = $(AOLSERVER)/bin
CFLAGS = -g -I$(AOLSERVER)/include
NSD=mynsd
OBJ=mynsd.o
RM=/bin/rm -f
CP=/bin/cp
LIBS=-ldl -lm -lz -lpthread 

$(NSD): $(OBJ)
	$(CC) -o $(NSD) $(OBJ) \
		$(LIB)/libnssock.a $(LIB)/libnscgi.a \
		$(LIB)/libnsd.a $(LIB)/libnsthread.a \
		$(LIB)/libnslog.a $(LIB)/libnsperm.a \
		$(LIB)/libtcl8.4g.a \
		$(LIBS)

install: $(NSD)
	$(RM) $(BIN)/$(NSD)
	$(CP) $(NSD) $(BIN)/

clean:
	$(RM) $(NSD) $(OBJ)
