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
# $Header: /Users/dossy/Desktop/cvs/aolserver/Makefile,v 1.44.2.1 2004/06/03 17:58:57 rcrittenden0569 Exp $
#

NSBUILD=1
include include/Makefile.global

dirs   = nsthread nsd nssock nsssl nscgi nscp nslog nsperm nsdb nsext nspd

all: 
	@for i in $(dirs); do \
		( cd $$i && $(MAKE) all ) || exit 1; \
	done

install: install-binaries install-doc

install-binaries: all
	for i in bin lib log include modules/tcl servers/server1/pages; do \
		$(MKDIR) $(AOLSERVER)/$$i; \
	done
	for i in include/*.h include/Makefile.global include/Makefile.module; do \
		$(INSTALL_DATA) $$i $(AOLSERVER)/include/; \
	done
	for i in tcl/*.tcl; do \
		$(INSTALL_DATA) $$i $(AOLSERVER)/modules/tcl/; \
	done
	$(INSTALL_DATA) sample-config.tcl $(AOLSERVER)/
	$(INSTALL_SH) install-sh $(INSTBIN)/
	for i in $(dirs); do \
		(cd $$i && $(MAKE) install) || exit 1; \
	done

install-tests:
	$(CP) -r tests $(INSTSRVPAG)

install-doc:
	cd doc && /bin/sh ./install-doc $(AOLSERVER)

clean:
	@for i in $(dirs); do \
		(cd $$i && $(MAKE) clean) || exit 1; \
	done

distclean: clean
	$(RM) config.status config.log config.cache include/Makefile.global include/Makefile.module

.PHONY: all install install-binaries install-doc install-tests clean distclean
