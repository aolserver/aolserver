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
# $Header: /Users/dossy/Desktop/cvs/aolserver/tcl/form.tcl,v 1.5 2002/02/08 07:56:16 hobbs Exp $
#

#
# form.tcl -- Handle url-encoded or multi-part data for forms
#


proc ns_queryget {key {value ""}}  {
    set form [ns_getform]
    if { $form != "" } {
	set tmp [ns_set iget $form $key]
	if {[string length $tmp]} {
	    set value $tmp
	}
    }
    return $value
}

proc ns_querygetall {key {def_result ""}} {
    set form [ns_getform]
    if {$form != ""} {
        set result ""
        set size [ns_set size $form]
        set lkey [string tolower $key]
        # loop over all keys in the formdata, find all that case-
        # insensitively match the passed-in key, and append the values
        # a a return list.
        for {set i 0} {$i < $size} {incr i} {
            set k [ns_set key $form $i]
            if {[string tolower $k] == $lkey} {
                lappend result [ns_set value $form $i]
            }
         }
     } else {
         set result $def_result
     }
     return $result
}

#
# ns_queryexists --
#
#	Check if a form key exists.
#

proc ns_queryexists { key } {
    set form [ns_getform]
    set i -1
    if { $form != "" } {
	set i [ns_set ifind $form $key]
    }
    return [expr {$i >= 0}]
}


#
# ns_getform --
#
#	Return the connection form, spooling multipart form data
#	into temp files if necessary.
#

proc ns_getform { }  {
    global _ns_form

    if {![info exists _ns_form]} {
	set _ns_form [ns_conn form]
	foreach {n i} [ns_conn files] {
		set o [lindex $i 0]
		set l [lindex $i 1]
	    	set fp ""
	    	while {$fp == ""} {
			set tmpfile [ns_tmpnam]
			set fp [ns_openexcl $tmpfile]
	    	}
		fconfigure $fp -translation binary -encoding binary
		ns_conn copy $o $l $fp
		close $fp
		ns_atclose "ns_unlink -nocomplain $tmpfile"
	    	ns_set put $_ns_form $n.tmpfile $tmpfile
	}
    }
    return $_ns_form
}


#
# ns_openexcl --
#
#	Open a file with exclusive rights.  This call will fail if 
#	the file already exists in which case "" is returned.

proc ns_openexcl file {
    if {[catch { set fp [open $file {RDWR CREAT EXCL} ] } err]} {
	global errorCode
	if { [lindex $errorCode 1] != "EEXIST"} {
	    return -code error $err
	}
	return ""
    }
    return $fp
}

