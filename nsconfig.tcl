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
# nsconfig.tcl --
#
#	Configure build of AOLserver.  See usage proc for details.
#


#
# Set arg defaults.
#

set tclsh [file native [info nameofexecutable]]
set tcldir [file native [file dirname [file dirname $tclsh]]]
set install $tcldir
set debug 0

#
# Utility procs used below.
#

proc err str {
	puts stderr $str
	exit 1
}

proc usage {{ishelp 0}} {
	global install argv0 
	set msg {Usage: $argv0 ?-help? ?-install dir? ?-debug?

Where:
	-help		This message.
	-install dir	Specify path to install (default: $install)
	-debug		Debug build with symbols and without optimization.
}
	set msg [subst $msg]
	if !$ishelp {
		err $msg
	}
	puts $msg
	exit 0
}


#
# Parse arg list.
#

set i 0
while {$i < $argc} {
	set opt [lindex $argv $i]
	switch -glob -- $opt {
		-h* {
			usage 1
		}
		-i* {
			set install [lindex $argv [incr i]]
			if ![string length $install] {
				usage
			}
		}
		-d* {
			set debug 1
		}
		default {
			usage
		}
	}
	incr i
}

#
# Validate arguments.
#

set install [file native $install]
if {[file exists $install] && ![file isdirectory $install]} {
	err "Invalid install directory: $install"
}


#
# On Unix, call configure shell script and exit.
#

if [string equal $tcl_platform(platform) unix] {
	set configure [list ./configure TCLSH=$tclsh --prefix=$install --with-tcl=$tcldir/lib]
	if $debug {
		lappend configure --enable-symbols
	}
	puts "Executing $configure"
	eval exec $configure >&@ stdout
	exit 0
}


#
# On Windows, determine and verify Tcl library and includes.
#

if ![info exists tcl_platform(threaded)] {
	err "Tcl not built with threads enabled."
}
set tclinc [file native $tcldir/include]
if ![file exists $tclinc] {
	err "Missing Tcl include directory: $tclinc"
}
scan [info tclversion] %d.%d major minor
set tcllib $tcldir/lib/tcl${major}${minor}t
if [info exists tcl_platform(debug)] {
	append tcllib g
}
set tcllib [file native [append tcllib .lib]]
if ![file exists $tcllib] {
	err "Missing Tcl lib: $tcllib"
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
puts "	AOLSERVER=$install"
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
puts $fp "CONFIG_AOLSERVER=$install"
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
