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
# $Header: /Users/dossy/Desktop/cvs/aolserver/Makefile,v 1.22 2001/05/28 21:58:12 jgdavidson Exp $
#

#
# Tell make where AOLserver source code lives.
#

NSHOME    =  $(shell pwd)
MAKEFLAGS += NSHOME=$(NSHOME)

include $(NSHOME)/include/Makefile.global

#
# The nsthread, Tcl, nsd libraries and nsd main directories.
#

DIRS	= thread $(NSTCL_ROOT) nsd nsmain

#
# Optional module directories:
#
#   nssock      -- serves HTTP
#   nsssl       -- serves HTTPS
#   nscgi       -- CGI module
#   nscp        -- Control port remote administration interface
#   nslog       -- Common log format module
#   nsperm      -- Permissions module
#   nsext       -- External database driver module
#   nspd        -- Archive library for building an external driver
#

DIRS	+= nssock nsssl nscgi nscp nslog nsperm nsext nspd 

all:
	@for i in $(DIRS); do \
		$(ECHO) "building \"$$i\""; \
		( cd $$i && $(MAKE) all ) || exit 1; \
	done

install: all
	$(MKDIR)                    $(INSTBIN)
	$(MKDIR)                    $(INSTLOG)
	$(MKDIR)                    $(INSTTCL)
	$(MKDIR)                    $(INSTLIB)
	$(MKDIR)                    $(INSTSRVMOD)
	$(MKDIR)                    $(INSTSRVPAG)
	$(CP) -r tcl                $(INSTMOD)
	$(CP) -r include            $(INSTINC)
	test -f $(INSTSRVPAG)/index.html \
		|| $(CP) doc/default-home.html $(INSTSRVPAG)/index.html
	@for i in $(DIRS); do \
		$(ECHO) "installing \"$$i\""; \
		( cd $$i && $(MAKE) install) || exit 1; \
	done

install-tests:
	$(CP) -r tests $(INSTSRVPAG)

clean: 
	@for i in $(DIRS); do \
		$(ECHO) "cleaning \"$$i\""; \
		( cd $$i && $(MAKE) $@) || exit 1; \
	done

distclean: clean
	(cd $(NSTCL_ROOT); make distclean)
