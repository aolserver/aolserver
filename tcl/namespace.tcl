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
# $Header: /Users/dossy/Desktop/cvs/aolserver/tcl/Attic/namespace.tcl,v 1.3 2000/08/02 23:38:25 kriston Exp $
#

#
# namespace.tcl --
#
#	Procedures to generate a script to initialize
#	a new interp using namespaces.
#


#
# If the namespace command does not exist, leave
# the simple global-only procedure defined in
# C in place which works for Tcl7.6.
#

if {[info command namespace] == ""} {
	return
}


#
# _ns_getinit -
#
#	Return a script to initialize new interps.
#	AOLserver will invoke this script in the
#	initial thread's interp at startup and
#	after an ns_eval to generate a script for
#	initializing new interps.
#

proc _ns_getinit {} {
    ns_log notice "tcl: generating interp init script"
    _ns_getnamespaces namespaces
    set init ""
    foreach ns $namespaces {
	append init [list namespace eval $ns [_ns_getnamespace $ns]]\n
    }
    return $init
}


#
# _ns_getnamespaces -
#
#	Recursively append the list of all known namespaces
#	to the variable named by listVar variable.
#

proc _ns_getnamespaces {listVar {n ""}} {
    upvar $listVar list
    lappend list $n
    foreach c [namespace children $n] {
	_ns_getnamespaces list $c
    }
}


#
# _ns_getnamespace --
#
#	Return a script to create a namespace.
#

proc _ns_getnamespace n {
    namespace eval $n {
	set n [namespace current]
	set script ""
	foreach v [info vars] {
	    switch $v {
		n -
		v -
		script continue
		default {
		    if [info exists ${n}::$v] {
			if [array exists $v] {
			    append script [list variable $v]\n
			    append script [list array set $v [array get $v]]\n
			} else {
			    append script [list variable $v [set $v]]\n
			}
		    }
		}
	    }
	}
	foreach p [info procs] {
	    set args ""
	    foreach a [info args $p] {
		if [info default $p $a def] {
		    set a [list $a $def]
		}
		lappend args $a
	    }
	    append script [list proc $p $args [info body $p]]\n
	}
	return $script
    }
}
