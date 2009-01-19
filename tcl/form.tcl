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
# $Header: /Users/dossy/Desktop/cvs/aolserver/tcl/form.tcl,v 1.10 2009/01/19 12:21:08 gneumann Exp $
#

#
# form.tcl -- Handle url-encoded or multi-part forms.
#
# Multi-part forms are described in RFC 1867:
#
#	http://www.ietf.org/rfc/rfc1867.txt
#
# Briefly, use:
#
#	<form enctype="multipart/form-data" action="url" method=post>
#	First file: <input name="file1" type="file">
#	Second file: <input name="file2" type="file">
#	<input type="submit">
#	</form>
#
# and then access with:
#
#	set tmpfile1 [ns_getformfile file1]
#	set tmpfile2 [ns_getformfile file2]
#	set fp1 [open $tmpfile1]
#	set fp2 [open $tmpfile2]
#
# Temp files created by ns_getform are removed when the connection closes.
#


#
# ns_queryget --
#
#	Get a value from the http form.
#

proc ns_queryget {key {value ""}}  {
    set form [ns_getform]
    if { $form ne "" } {
	set tmp [ns_set iget $form $key]
	if {[string length $tmp]} {
	    set value $tmp
	}
    }
    return $value
}


#
# ns_querygetall --
#
#	Get all values of the same key name from the http form.
#

proc ns_querygetall {key {def_result ""}} {
    set form [ns_getform]
    if {$form ne ""} {
        set result ""
        set size [ns_set size $form]
        set lkey [string tolower $key]
        # loop over all keys in the formdata, find all that case-
        # insensitively match the passed-in key, and append the values
        # a a return list.
        for {set i 0} {$i < $size} {incr i} {
            set k [ns_set key $form $i]
            if {[string tolower $k] == $lkey} {
                if {[string length [ns_set value $form $i]]} {
                    lappend result [ns_set value $form $i]
                }
            }
        }
        if {$result eq ""} {
            set result $def_result
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
    if { $form ne "" } {
	set i [ns_set ifind $form $key]
    }
    return [expr {$i >= 0}]
}


#
# ns_getform --
#
#	Return the connection form, copying multipart form data
#	into temp files if necessary.
#

proc ns_getform {{charset ""}}  {
    global _ns_form _ns_formfiles

    #
    # If a charset has been specified, use ns_urlcharset to
    # alter the current conn's urlcharset.
    # This can cause cached formsets to get flushed.
    if {$charset ne ""} {
	ns_urlcharset $charset
    }

    if {![info exists _ns_form]} {
	set _ns_form [ns_conn form]
	foreach {file} [ns_conn files] {
		set off [ns_conn fileoffset $file]
		set len [ns_conn filelength $file]
		set hdr [ns_conn fileheaders $file]
		set type [ns_set get $hdr content-type]
	    	set fp ""
	    	while {$fp eq ""} {
			set tmpfile [ns_tmpnam]
			set fp [ns_openexcl $tmpfile]
	    	}
		fconfigure $fp -translation binary 
		ns_conn copy $off $len $fp
		close $fp
		ns_atclose "ns_unlink -nocomplain $tmpfile"
		set _ns_formfiles($file) $tmpfile
	    	ns_set put $_ns_form $file.content-type $type
		# NB: Insecure, access via ns_getformfile.
	    	ns_set put $_ns_form $file.tmpfile $tmpfile
	}
    }
    return $_ns_form
}


#
# ns_getformfile --
#
#	Return a tempfile for a form file field.
#

proc ns_getformfile {name} {
    global _ns_formfiles

    ns_getform
    if {![info exists _ns_formfiles($name)]} {
	return ""
    }
    return $_ns_formfiles($name)
}


#
# ns_openexcl --
#
#	Open a file with exclusive rights.  This call will fail if 
#	the file already exists in which case "" is returned.

proc ns_openexcl file {
    if {[catch { set fp [open $file {RDWR CREAT EXCL} ] } err]} {
	global errorCode
	if { [lindex $errorCode 1] ne "EEXIST"} {
	    return -code error $err
	}
	return ""
    }
    return $fp
}


#
# ns_resetcachedform --
#
#	Reset the http form set currently cached (if any),
#       optionally to be replaced by the given form set.
#

proc ns_resetcachedform { { newform "" } } {
    global _ns_form

    if {[info exists _ns_form]} {
	unset _ns_form
    }
    if {$newform ne "" } {
        set _ns_form $newform
    }
}


#
# ns_isformcached --
#
#	Predicate function to answer whether there is
#       a http form set currently cached.
#

proc ns_isformcached { } {
    global _ns_form
    return [info exists _ns_form]
}



