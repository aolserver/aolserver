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
# $Header: /Users/dossy/Desktop/cvs/aolserver/nsssl/Makefile,v 1.5 2002/10/03 22:41:45 jgdavidson Exp $
#



ifndef BSAFE

all:
	@echo "** "
	@echo "** BSAFE variable not set."
	@echo "** nsssl will not be built."
	@echo "** "

install: all

clean:

else

#
# Module name
#
ifeq ($(SSL_DOMESTIC),1)
  MOD    =  nsssl.so
  CFLAGS =  $(BSAFE_VERSION) -I$(BSAFE)/include
else
  MOD    =  nsssle.so
  CFLAGS =  -DSSL_EXPORT=1 $(BSAFE_VERSION) -I$(BSAFE)/include
endif
MODLIBS  =  -L$(BSAFE)/lib -lbsafe
MODDIR   =  nsssl
HDRS     =  ssl.h ssltcl.h x509.h
LDRPATH  += $(LDRFLAG)$(BSAFE)/lib


#
# Rainbow SSL accelerator card support.
#
#   Note: Requires position-independent version of libbswift.a.
#         Requires libswift.so to be somewhere in your system's library path.
#         Contact Rainbow at http://www.rainbow.com/ for the software.
#
ifdef SWIFT
  CFLAGS  +=-DHAVE_SWIFT=1 -I$(SWIFT)/include
  MODLIBS +=-L$(SWIFT)/lib -lbswift -lswift
endif


#
# Objects to build
#
OBJS      =  nsssl.o ssl.o ssltcl.o t_stdlib.o x509.o
MODINST	  =  cppem

include  ../include/Makefile.build

cppem:
	$(MKDIR) $(INSTMOD)/nsssl
	$(INSTALL_DATA) keygen.tcl $(INSTMOD)/nsssl
	$(MKDIR) $(INSTSRVMOD)/nsssl
	$(INSTALL_DATA) certfile-sample.pem $(INSTSRVMOD)/nsssl
	$(INSTALL_DATA) keyfile-sample.pem $(INSTSRVMOD)/nsssl

endif
