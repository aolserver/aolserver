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
# $Header: /Users/dossy/Desktop/cvs/aolserver/Makefile,v 1.63 2006/04/13 20:11:57 jgdavidson Exp $
#
#

bins=nsthread nsd nstclsh 
mods=nsdb nssock nslog nsperm nscgi nscp
dirs=$(bins) $(mods)

SRCDIR=.
include include/ns.mak

all: build

build:
	$(MAKEALL) build $(dirs)

clean:
	$(MAKEALL) clean $(dirs)

install: install-bins install-includes install-util install-tcl \
	 install-mods install-skel

install-bins:
	$(MAKEALL) install $(bins)

install-mods:
	$(MAKEALL) install $(mods)

install-util:
	$(INST) -d $(INSTBIN) util/*.tcl
	$(INST) -d $(INSTBIN) -e util/nsinstall-man.sh

install-tcl:
	$(INST) -d $(AOLSERVER)/modules/tcl tcl/*.tcl

install-includes:
	$(INST) -d $(INSTINC) include/*.c include/*.h include/ns.mak \
			      include/Makefile.*

install-skel:
	$(INST) -d $(AOLSERVER)/log
	$(INST) -d $(AOLSERVER) examples/config/base.tcl
	$(INST) -d $(AOLSERVER)/servers/server1/pages -n index.adp

install-docs:
	$(MAKEALL) install doc

distclean: clean
	$(RM) include/ns.mak include/ns.bak \
		config.status config.log config.cache
