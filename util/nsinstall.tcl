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
# nsinstall.tcl --
#
#	Install script for AOLserver.
#


proc usage {{status 1}} {
	puts {Usage:

	nsinstall.tcl ?-d dir? ?-f file? ?-n? -?-e? ?--? pattern ?pattern ...?

Where:
	-h		This message.
	-d dir		Destination directory.
	-f file		Copy as specific filename.
	-n		Do not overwrite existing files.
	-e		Install with execute permission.
	--		Specifies end of args.
	pattern		One or more file glob patterns (e.g., foo.c, *.tcl)

One of -f or -d must be specified}
	exit $status
}


set pid [pid]
set mode 0644
set overwrite 1
set iswin [string equal $tcl_platform(platform) windows]
if $iswin {
	set modestr ""
} else {
	set modestr " ($mode)"
}


#
# Process option arguments.
#

set i 0
while {$i < $argc} {
	set arg [lindex $argv $i]
	switch -glob -- $arg {
		-h {
			usage 0
		}
		-d {
			set dir [lindex $argv [incr i]]
		}
		-f {
			set file [lindex $argv [incr i]]
		}
		-e {
			set mode 0755
		}
		-n {
			set overwrite 0
		}
		-* {
			usage
		}
		-- -
		default {
			break
		}
	}
	incr i
}

#
# Get list of files from remaining args.
#

set files ""
while {$i < $argc} {
	foreach f [glob -nocomplain [lindex $argv $i]] {
		lappend files $f
	}
	incr i
}


#
# Sanity checks.
#

if [info exists file] {
	if {[llength $files] > 1} {
		usage
	}
	set file [file normalize $file]
	set dir [file dirname $file]
	if {[file exists $dir] && ![file isdirectory $dir]} {
		usage
	}
}
if ![info exists dir] {
	usage
}


#
# Create directory and copy file(s).
#

file mkdir $dir

foreach src $files {
	set src [file normalize $src]
	if [info exists file] {
		set tail [file tail $file]
	} else {
		set tail [file tail $src]
	}
	set dst [file normalize $dir/$tail]
	set tmp [file normalize $dir/.#inst.$tail.$pid]
	incr i
	if [file exists $dst] {
		if !$overwrite {
			puts "exists: $dst"
			continue
		}
		file delete $dst
	}
	file copy $src $tmp
	if {!$iswin} {
		file attributes $tmp -permission $mode
	}
	file rename $tmp $dst
	puts "installed: $dst$modestr"
}
