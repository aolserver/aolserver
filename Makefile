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
# $Header: /Users/dossy/Desktop/cvs/aolserver/Makefile,v 1.24 2001/11/05 20:30:26 jgdavidson Exp $
#

#
# You may set the following variables here, on the make command line,
# or via shell environment variables.
#

#
# Location of AOLserver sources (this directory).
#

ifndef NSHOME
    NSHOME	= $(shell pwd)
endif

#
# AOLserver installation directory where files are copied and
# shared libraries and Tcl files are searched for at runtime.
#

ifndef AOLSERVER
    AOLSERVER	= /usr/local/aolserver
endif

#
# Compile for debugging and with GCC if available.
#

ifndef NSDEBUG
    NSDEBUG	= 1
endif
ifndef NSGCC
    NSGCC	= 1
endif

#
# Modules to build.
#

ifndef NSMODS	
    NSMODS	= nssock nsssl nscgi nscp nslog nsperm nsext nspd 
endif


##################################################################
#
# You should not need to edit anything below.
#
##################################################################

include include/Makefile.global

MAKEFLAGS 	+= NSHOME=$(NSHOME) NSDEBUG=$(NSDEBUG) NSGCC=$(NSGCC)
DIRS		=  $(NSTCL_ROOT) nsd $(NSMODS)

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
	(cd $(NSTCL_ROOT); $(MAKE) distclean)
