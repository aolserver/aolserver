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
# $Header: /Users/dossy/Desktop/cvs/aolserver/tcl/form.tcl,v 1.3.10.1 2002/10/28 23:16:08 jgdavidson Exp $
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
    if {$form != ""} {
	set tmp [ns_set iget $form $key]
	if [string length $tmp] {
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
    return [expr $i >= 0]
}


#
# ns_getform --
#
#	Return the connection form, spooling multipart form data
#	into temp files if necessary.
#

proc ns_getform {} {
    global _ns_form

    if ![info exists _ns_form] {
	set _ns_form ""
	set method [ns_conn method]
	set type [ns_set iget [ns_conn headers] content-type]
	
	## MSIE redirected POST bug workaround.
	if {![string match "POST" $method] || \
		![string match "*multipart/form-data*" \
		      [string tolower $type]] } {
	    
	    # Return ordinary or non-existant form.
	    set _ns_form [ns_conn form]

	} else {

	    # Spool content into a temporary read/write file.
	    # ns_openexcl can fail, hence why we keep spinning
	    
	    set fp ""
	    while {$fp == ""} {
		set tmpfile [ns_tmpnam]
		set fp [ns_openexcl $tmpfile]
	    }
            fconfigure $fp -translation binary -encoding binary
	    ns_conncptofp $fp
	    seek $fp 0
	    set _ns_form [_ns_parseformfp $fp $type]
	    close $fp
	    ns_unlink $tmpfile
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
    global _ns_form_files

    ns_getform
    if {![info exists _ns_form_files($name)]} {
	return ""
    }
    return $_ns_form_files($name)
}


#
# _ns_parseformfp --
#
#	Parse a multi-part form data file.
#

proc _ns_parseformfp {fp contentType} {
    global _ns_form_files

    # parse the boundary out of the content-type header

    regexp -nocase {boundary=(.*)$} $contentType 1 b
    set boundary "--$b"

    set form [ns_set create $boundary]

    while { ![eof $fp] } {
	# skip past the next boundary line
	if ![string match $boundary* [string trim [gets $fp]]] {
	    continue
	}

	# fetch the disposition line and field name
	set disposition [string trim [gets $fp]]
	if ![string length $disposition] {
	    break
	}

	set disposition [split $disposition \;]
	set name [string trim [lindex [split [lindex $disposition 1] =] 1] \"]

	# fetch and save any field headers (usually just content-type for files)
	
	while { ![eof $fp] } {
	    set line [string trim [gets $fp]]
	    if ![string length $line] {
		break
	    }
	    set header [split $line :]
	    set key [string tolower [string trim [lindex $header 0]]]
	    set value [string trim [lindex $header 1]]
	    
	    ns_set put $form $name.$key $value
	}

	if { [llength $disposition] == 3 } {
	    # uploaded file -- save the original filename as the value
	    set filename [string trim \
	    		  [lindex [split [lindex $disposition 2] =] 1] \"]
	    ns_set put $form $name $filename

	    # read lines of data until another boundary is found
	    set start [tell $fp]
	    set end $start
	    
	    while { ![eof $fp] } {
		if [string match $boundary* [string trim [gets $fp]]] {
		    break
		}
		set end [tell $fp]
	    }
	    set length [expr $end - $start - 2]

	    # create a temp file for the content, which will be deleted
	    # when the connection close.  ns_openexcl can fail, hence why 
	    # we keep spinning

	    set tmp ""
	    while { $tmp == "" } {
		set tmpfile [ns_tmpnam]
		set tmp [ns_openexcl $tmpfile]
	    }
	    if { $length > 0 } {
		seek $fp $start
		ns_cpfp $fp $tmp $length
	    }
	    ns_atclose "ns_unlink -nocomplain $tmpfile"
	    close $tmp
	    seek $fp $end
	    set _ns_form_files($name) $tmpfile
	    # NB: Potential security risk, access file via ns_getformfile.
	    ns_set put $form $name.tmpfile $tmpfile

	} else {
	    # ordinary field - read lines until next boundary
	    set first 1
	    set value ""
	    set start [tell $fp]

	    while { [gets $fp line] >= 0 } {
		set line [string trimright $line \r]
		if [string match $boundary* $line] {
		    break
		}
		if $first {
		    set first 0
		} else {
		    append value \n
		}
		append value $line
		set start [tell $fp]
	    }
	    seek $fp $start
	    ns_set put $form $name $value
	}
    }
    return $form
}


#
# ns_openexcl --
#
#	Open a file with exclusive rights.  This call will fail if 
#	the file already exists in which case "" is returned.
#

proc ns_openexcl file {
    if [catch { set fp [open $file {RDWR CREAT EXCL} ] } err] {
	global errorCode
	if { [lindex $errorCode 1] != "EEXIST"} {
	    return -code error $err
	}
	return ""
    }
    return $fp
}

