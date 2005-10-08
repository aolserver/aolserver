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
# nsmakeall.tcl --
#
#	Execute make for given target in each subdirectory in given list.
#
# Usage:
#	nsmakeall.tcl target dir ?dir dir ...?
#
# Where:
#	target		Make target (e.g., "clean")
#	dir		One or more sub-directories to make.
#



if {$argc < 2} {
	puts "Usage: $argv0 target dir ?dir dir ...?"
	exit 1
}
if ![string equal $tcl_platform(platform) unix] {
	set make nmake
} else {
	# Look for gmake if available in the path, otherwise try make.
	set make make
	foreach p [split $env(PATH) :] {
		if [file executable $p/gmake] {
			set make $p/gmake
			break
		}
	}
}
set target [lindex $argv 0]
set srcdir [file native [pwd]]
for {set i 1} {$i < $argc} {incr i} {
	set dir [lindex $argv $i]
	cd $srcdir/$dir
	puts "make $target: $dir"
	if [catch {exec $make SRCDIR=$srcdir $target >&@ stdout}] {
		exit 1
	}
}
