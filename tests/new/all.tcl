#!/bin/sh
# the next line restarts using tclsh \
exec tclsh "$0" "$@"

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
# $Header: /Users/dossy/Desktop/cvs/aolserver/tests/new/all.tcl,v 1.3 2004/09/08 00:07:50 dossy Exp $
#

package require Tcl 8.4

if {![info exists ::tcl_platform(threaded)] || !$::tcl_platform(threaded)} {
    error "tests must run from a threaded tclsh"
}

package require tcltest 2.2

set LD_LIBRARY_PATH [list]
if {[info exists env(LD_LIBRARY_PATH)]} {
    lappend LD_LIBRARY_PATH $env(LD_LIBRARY_PATH)
}
lappend LD_LIBRARY_PATH ../../nsd ../../nsthread
set env(LD_LIBRARY_PATH) [join $LD_LIBRARY_PATH :]

tcltest::configure -testdir [file dirname [info script]]
eval tcltest::configure $argv

tcltest::runAllTests
