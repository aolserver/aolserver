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
# $Header: /Users/dossy/Desktop/cvs/aolserver/doc/Makefile,v 1.1 2005/07/18 23:33:47 jgdavidson Exp $
#

AOLSERVER ?= /usr/local/aolserver
include $(AOLSERVER)/include/Makefile.global

MAN3 := $(wildcard *.3)
MANN := $(wildcard *.n)
MAN1 := $(wildcard *.1)
MAN_DIR = $(AOLSERVER)/man
MAN1_DIR = $(MAN_DIR)/man1
MAN3_DIR = $(MAN_DIR)/man3
MANN_DIR = $(MAN_DIR)/mann
MAN_INST = nsinstall-man.sh
NSLIBS = $(wildcard $(AOLSERVER)/lib/libns*$(LIBEXT))

all: install

install: install-man1 install-man3 install-mann

install-man1:
	$(MKDIR) $(MAN1_DIR)
	for i in $(MAN1); do \
		sh $(MAN_INST) $$i $(MAN1_DIR)/; \
	done

install-man3:
	for i in $(MAN3); do \
		sh $(MAN_INST) $$i $(MAN3_DIR)/; \
	done

install-mann:
	for i in $(MANN); do \
		sh $(MAN_INST) $$i $(MANN_DIR)/; \
	done

missing: 
	@nm $(NSLIBS) | awk '/T _Ns_/ {print $$3}' | sort > lib.out
	@sed -n '/^.SH NAME/,/^.SH DESC/p' $(MAN3) | sed -n '/^\\fBNs_\(.*\)\\fR(\(.*\))/s//_Ns_\1/p' \
		| sort | uniq | diff -y lib.out -
