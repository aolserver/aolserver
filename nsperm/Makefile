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
# $Header: /Users/dossy/Desktop/cvs/aolserver/nsperm/Makefile,v 1.2.10.3 2003/08/06 17:32:43 elizthom Exp $
#

MOD      =  nsperm.so
OBJS     =  nsperm.o
MODINST	 =  cpfiles

include ../include/Makefile.build

# Solaris 2.7+
ifneq (,$(findstring SunOS,$(uname)))
    LIBS+=-lresolv
endif

moddir	 = nsperm
moddat	 = $(INSTSRVMOD)/$(moddir)
modtcl	 = $(INSTTCL)/$(moddir)

cpfiles:
	$(MKDIR) $(modtcl)
	$(INSTALL_DATA) init.tcl $(modtcl)/
	$(INSTALL_DATA) compat.tcl $(modtcl)/
	$(MKDIR) $(moddat)
	for f in passwd group hosts.allow hosts.deny perms; do \
		test -f $(moddat)/$$f || $(INSTALL_DATA) $$f $(moddat)/; \
	done
