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
# Usage:
#	nsinstall.tcl -d <dst> ?-n? -?-e? pattern ?pattern pattern ...?
#
# Where:
#	-d <dst>	Destination directory (required).
#	-n		Do not overwrite existing files.
#	-e		Install with execute permission.
#	pattern		One or more file glob patterns (e.g., foo.c, *.tcl)
#


set exec 0
set overwrite 1
set done 0
set i 0
set iswin [string equal $tcl_platform(platform) windows]

#
# Process arguments.
#

while {$i < $argc} {
	set arg [lindex $argv $i]
	switch -glob -- $arg {
		-d {
			set dir [lindex $argv [incr i]]
		}
		-e {
			set exec 1
		}
		-n {
			set overwrite 0
		}
		-* {
			puts "Unknown option: $arg"
			exit 1
		}
		default {
			set done 1
		}
	}
	if $done break
	incr i
}


#
# Create destination directory.
#

if ![info exists dir] {
	puts "Missing required -d <dir> arg"
	exit 1
}
file mkdir $dir


#
# Copy files specified by each glob pattern.
#

while {$i < $argc} {
	foreach src [glob -nocomplain [lindex $argv $i]] {
		set src [file normalize $src]
		set dst [file normalize $dir/[file tail $src]]
		incr i
		if {!$overwrite && [file exists $dst]} {
			puts "exists: $dst"
			continue
		}
		file copy -force $src $dst
		if {!$iswin} {
			file attributes $dst -permission 755
			puts "chmod: $dst"
		}
		puts "install: $dst"
	}
}
