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
# $Header: /Users/dossy/Desktop/cvs/aolserver/Makefile,v 1.34 2002/06/12 22:41:40 jgdavidson Exp $
#

#
# You may set the following variables here, on the make command line,
# or via shell environment variables.
#
# AOLSERVER:	AOLserver install directory (/usr/local/aolserver)
# DEBUG		Build with debug symbols (default: 0, no symbols)
# GCC		Build with gcc compiler (default: 1, use gcc)
#


##################################################################
#
# You should not need to edit anything below.
#
##################################################################

include include/Makefile.global

MAKEFLAGS 	+= nsbuild=1
ifdef AOLSERVER
    MAKEFLAGS	+= AOLSERVER=$(AOLSERVER)
endif
ifdef NSDEBUG
    MAKEFLAGS	+= NSDEBUG=$(NSDEBUG)
endif
ifdef NSGCC
    MAKEFLAGS	+= NSGCC=$(NSGCC)
endif

dirs   = nsthread nsd nssock nsssl nscgi nscp nslog nsperm nsdb nsext nspd

all: tcl aolserver

aolserver:
	@for i in $(dirs); do \
		( cd $$i && $(MAKE) all ) || exit 1; \
	done

install: all install-tcl install-aolserver

install-binaries: all install-tclbinaries install-asbinaries

install-aolserver: install-asbinaries
	$(MKDIR)		$(AOLSERVER)/log
	$(MKDIR)		$(AOLSERVER)/modules
	$(MKDIR)		$(INSTSRVPAG)
	$(CP) -r tcl    	$(AOLSERVER)/modules/
	$(CP) -r include	$(AOLSERVER)/
	$(CP) sample-config.tcl $(AOLSERVER)/

install-asbinaries: 
	@for i in $(dirs); do \
		(cd $$i && $(MAKE) install) || exit 1; \
	done

install-tests:
	$(CP) -r tests $(INSTSRVPAG)

distclean: clean-aolserver
	(cd $(tclsrc); $(MAKE) -f Makefile.in distclean)

clean: clean-tcl clean-aolserver

clean-aolserver:
	@for i in $(dirs); do \
		(cd $$i && $(MAKE) clean) || exit 1; \
	done

clean-tcl:
	(cd $(tclsrc); $(MAKE) -f Makefile.in clean)

tcl: $(tclsrc)/Makefile 
	(cd $(tclsrc); $(MAKE) MEM_DEBUG_FLAGS='$(tclmemdbg)')

tcl-checkout:
	(cd `dirname $(tcldir)` && \
		cvs -d :pserver:anonymous@cvs.tcl.sourceforge.net:/cvsroot/tcl \
			co -d `basename $(tcldir)` tcl)

tcl-update:
	(cd $(tcldir) && cvs update)

$(tclsrc)/Makefile: $(tclsrc)/Makefile.in $(tclsrc)/configure
	(cd $(tclsrc); ./configure $(tclcfg))

install-tcl: install-tclbinaries
	(cd $(tclsrc); $(MAKE) install-libraries)

install-tclbinaries: tcl
	(cd $(tclsrc); $(MAKE) install-binaries)
