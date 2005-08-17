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
# configure.tcl --
#
#	Configure build of AOLserver.  By default, matches the build
#	and install environment of cooresponding Tcl directory.
#
# Usage:
#	tclsh configure.tcl [install-dir]
#
# Where:
#	install-dir	Path to install AOLserver (default: Tcl directory)
#


#
# Sanity check and basic vars.
#

if ![info exists tcl_platform(threaded)] {
	puts "Tcl not built with threads enabled."
	exit 1
}
set tclsh [file native [info nameofexecutable]]
set tcldir [file native [file dirname [file dirname $tclsh]]]
set debug [info exists tcl_platform(debug)]
if {$argc < 1} {
	set aolserver $tcldir
} else {
	set aolserver [file native [lindex $argv 0]]
}
if [string equal $tcl_platform(platform) unix] {
	set args [list --prefix=$aolserver --with-tcl=$tcldir/lib]
	if $debug {
		lappend args --enable-symbols
	}
	eval exec ./configure TCLSH=$tclsh $args >&@ stdout
	exit 0
}


#
# Determine Tcl version.
#

scan [info tclversion] %d.%d major minor

#
# Set and verify Tcl config variables.
#

set tclinc [file native $tcldir/include]
if ![file exists $tclinc/tcl.h] {
	puts "Can't find Tcl header $tclinc/tcl.h"
	exit 1
}
if $debug {
	set g g
} else {
	set g ""
}
set tcllib [file native $tcldir/lib/tcl${major}${minor}t${g}.lib]
if ![file exists $tcllib] {
	puts "Can't find Tcl library $tcllib"
	exit 1
}


#
# Create ns.mak.
#


set makout include/ns.mak
set makbak include/ns.bak
set makinc include/ns-mak.inc
if ![file exists $makinc] {
	puts "Missing $makinc"
	exit 1
}
if [file exists $makout] {
	file copy -force $makout $makbak
}
puts "Configuring $makout with variables:"
puts "	AOLSERVER=$aolserver"
puts "	DEBUG=$debug"
puts "	TCLSH=$tclsh"
puts "	TCLINC=$tclinc"
puts "	TCLLIB=$tcllib"
set fp [open $makout w]
puts $fp "#"
puts $fp "# Created by $argv0"
puts $fp "# [clock format [clock seconds]]"
puts $fp "#"
puts $fp ""
puts $fp "#"
puts $fp "# Config variables:"
puts $fp "#"
puts $fp ""
puts $fp "CONFIG_AOLSERVER=$aolserver"
puts $fp "CONFIG_DEBUG=$debug"
puts $fp "CONFIG_TCLSH=$tclsh"
puts $fp "CONFIG_TCLINC=$tclinc"
puts $fp "CONFIG_TCLLIB=$tcllib"
puts $fp ""
puts $fp "#"
puts $fp "# Shared $makinc"
puts $fp "#"
puts $fp ""
set ifp [open $makinc]
fcopy $ifp $fp
close $ifp
close $fp
