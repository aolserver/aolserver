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
# $Header: /Users/dossy/Desktop/cvs/aolserver/tcl/fastpath.tcl,v 1.11 2009/01/19 12:21:08 gneumann Exp $
#

#
# fastpath.tcl --
#
#   AOLserver 2.x fastpath routines moved from C.  The C code
#   now only handles the simple case of returning a file through
#   an optimized caching routines.  All legacy AOLpress publishing
#   functions are handled with the Tcl code below.  In addition,
#   the C code will dispatch to the _ns_dirlist proc below to
#   handle directory listings and AOLpress MiniWeb top pages.
#

set path "ns/server/[ns_info server]/fastpath"
nsv_set _ns_fastpath type [ns_config $path directorylisting none]
nsv_set _ns_fastpath hidedot [ns_config -bool $path hidedotfiles 1]
nsv_set _ns_fastpath toppage [ns_config -bool $path returnmwtoppage 0]
nsv_set _ns_fastpath aolpress [ns_config -bool $path enableaolpress 0]
nsv_set _ns_fastpath builddirs [ns_config -bool $path builddirs 0]
nsv_set _ns_fastpath serverlog [ns_config -bool $path serverlog 1]

#
# Register the publishing procs if enabled.  Note that you must
# load a permission module (e.g., nsperm) or the server
# will deny access to all these publishing methods.
#

if {[nsv_get _ns_fastpath aolpress]} {
    ns_register_proc PUT / _ns_publish _ns_put
    ns_register_proc DELETE / _ns_publish _ns_delete
    ns_register_proc BROWSE / _ns_publish _ns_browse
    ns_register_proc MKDIR / _ns_publish _ns_mkdir
    ns_register_proc OPTIONS / _ns_publish _ns_options
}


#
# _ns_isaolpress --
#
#   Is the client AOLpress?
#

proc _ns_isaolpress {} {
    set agent [ns_set iget [ns_conn headers] user-agent]
    return [string match NaviPress/* $agent]
}


#
# _ns_getnvd --
#
#   Return the MiniWeb control file for a directory.
#

proc _ns_getnvd dir {
    return $dir/document.nvd
}


#
# _ns_ismw --
#
#   Is the directory a MiniWeb?
#

proc _ns_ismw dir {
    return [file exists [_ns_getnvd $dir]]
}


#
# _ns_dirlist --
#
#   Handle directory listings.  This code is invoked from C.
#

proc _ns_dirlist {} {
    set url [ns_conn url]
    set dir [ns_url2file $url]
    set location [ns_conn location]

    if {[string index $url end] ne "/"} {
        ns_returnredirect "$location$url/"
        return
    }

    #
    # Handle special case of AOLpress MiniWebs.
    #

    if {[nsv_get _ns_fastpath toppage] && [_ns_ismw $dir]} {
	set nvd [_ns_getnvd $dir]
	if {[_ns_isaolpress]} {
	    return [ns_returnfile 200 application/x-navidoc $file]
	}
	set fp [open $nvd]
	while {[gets $fp line] >= 0} {
	    if {[string match Pages:* $line]} {
		break
	    }
	}
	gets $fp line
	close $fp
	set file [lindex [split $line \"] 1]
	if {[file exists $dir/$file]} { 
	    return [ns_returnredirect $file]
	}
    }

    #
    # Handle default case of directory listing.  Simple
    # format is just the files while fancy includes
    # the size and modify time (which is more expensive).
    #

    switch [nsv_get _ns_fastpath type] {
	simple {
	    set simple 1
	}
	fancy {
	    set simple 0
	}
	none -
	default {
	    return [ns_returnnotfound]
	}
    }

    set hidedot [nsv_get _ns_fastpath hidedot]
    
    set prefix "${location}${url}"
    set up "<a href=..>..</a>"
    if {$simple} {
	append list "
<pre>
$up
"
    } else {
	append list "
<table>
<tr align=left><th>File</th><th>Size</th><th>Date</th></tr>
<tr align=left><td colspan=3>$up</td></tr>
"
    }

    foreach f [lsort [glob -nocomplain $dir/*]] {
	set tail [file tail $f]
	if {$hidedot && [string match .* $tail]} {
	    continue
	}
        if {[file isdirectory $f]} { 
            append tail "/"
        }
	
	set link "<a href=\"${prefix}${tail}\">${tail}</a>"

	if {$simple} {
	    append list $link\n
	} else {
	    
	    if {[catch {
		file stat $f stat
	    } errMsg ]} {
		append list "
<tr align=left><td>$link</td><td>N/A</td><td>N/A</td></tr>\n
"
	    } else {
		set size [expr {$stat(size) / 1000 + 1}]K
		set mtime $stat(mtime)
		set time [clock format $mtime -format "%d-%h-%Y %H:%M"]
		append list "
<tr align=left><td>$link</td><td>$size</td><td>$time</td></tr>\n
"
	    }
	}
    }
    if {$simple} {
	append list "</pre>"
    } else {
	append list "</table>"
    }
    ns_returnnotice 200 $url $list
}


#
# _ns_publish --
#
#   Wrapper to log publishing actions.
#

proc _ns_publish proc {
    if {[nsv_get _ns_fastpath serverlog]} {
	ns_log notice "fastpath:[ns_conn authuser]:$proc [ns_conn url]"
    }
    $proc
}

proc ns_returnok {} {
    ns_return 200 text/plain ""
}

proc _ns_remove file {
    if {[nsv_get _ns_fastpath serverlog]} {
	ns_log notice "fastpath:[ns_conn authuser]:unlink: $file"
    }
    ns_unlink $file
}

#
# _ns_browse --
#
#   Handle the AOLpress BROWSE request to for its file dialog.
#
    
proc _ns_browse {} {
    set url [ns_conn url]
    set dir [ns_url2file $url]
    set files ""
    foreach f [glob -nocomplain $dir/*] {
	set tail [file tail $f]
	if {[file isdir $f]} {
	    set type "application/x-navidir"
	} else {    
	    set type [ns_guesstype $tail]
	}
	append files "$type $tail\n"
    }
    ns_return 200 application/x-navibrowse $files
}


#
# _ns_mkdir --
#
#   Handle the AOLpress MKDIR request to create a new directory.
#

proc _ns_mkdir {} {
    set url [ns_conn url]
    set dir [ns_url2file $url]
    if {[file exists $dir]} {
	return [ns_returnbadrequest "File Exists"]
    }

    if {[catch {ns_mkdir $dir} err]} {
	ns_log error "fastpath: mkdir $dir failed: $err"
	ns_returnbadrequest "Could not create directory"
    }
    ns_returnok
}


#
# _ns_options --
#
#   Handle the AOLpress OPTIONS request to return available functions.
#   Note that LOCK and UNLOCK are not yet supported.
#

proc _ns_options {} {
    set hdrs [ns_conn outputheaders]
    ns_set put $hdrs Allow "OPTIONS, GET, HEAD, PUT, BROWSE, MKDIR"
    ns_returnok
}


#
# _ns_delete --
#
#   Handle the AOLpress DELETE request to delete a file
#   or empty directory.  Note that deleting MiniWebs is
#   not yet supported.
#

proc _ns_delete {} {
    set url [ns_conn url]
    set file [ns_url2file $url]
    if {![file exists $file]} {
	return [ns_returnbadrequest "No Such File"]
    }
    if {[catch {
	if {![file isdir $file]} {
	    _ns_remove $file
	} else {
	    set dir $file
	    if {[_ns_ismw $dir]} {
		set nvd [_ns_getnvd $dir]
		set tail [file tail $nvd]
		set files($tail) 1
		set pages 0
		set fp [open $nvd]
		while {[gets $fp line] >= 0} {
		    if {[string match Pages:* $line]} {
			set pages 1
			break
		    }
		}
		if {$pages} {
		    while {[gets $fp line] >= 0} {
			if {![string match "\"*" $line]} break
			set file [lindex [split $line \"] 1]
			set files($file) 0
		    }
		}
		close $fp
		foreach file [glob -nocomplain $dir/*] {
		    set tail [file tail $file]
		    if {![info exists files($tail)]} {
			return [ns_returnbadrequest "Directory not empty"]
		    }
		    set files($tail) 1
		}
		foreach tail [array names files] {
		    if {$files($tail)} {
			_ns_remove $dir/$tail
		    }
		}
	    }
	    ns_rmdir $dir
	}
    } err]} {
	ns_log error "fastpath: delete $file failed: $err"
	ns_returnbadrequest "Could not delete file"
    }
    ns_returnok
}


#
# _ns_put --
#
#   Handle the AOLpress PUT request to save a file
#   or MiniWeb.  The MiniWeb code is a bit tricky.
#   Also, files are saved to tmp files and renamed
#   to ensure half-written files don't become permanent
#

proc _ns_put {} {
    set url [ns_conn url]
    set file [ns_url2file $url]
    set hdrs [ns_conn headers]
    set isdir 0
    set ismw 0
    set exists [file exists $file]
    set type [ns_set iget $hdrs content-type]

    if {[ns_set ifind $hdrs x-navicreate] < 0} {
	set create 0
    } else {
	set create 1
    }

    if {$create && $exists} {
	return [ns_returnerror 500 "Already Exists"]
    }
    if {$exists} {
	set isdir [file isdir $file]
	if {$isdir} {
	    set ismw [_ns_ismw $file]
	}
    }
    if {$type eq "application/x-naviwad"} {
	if {$exists} {
	    if {!$ismw} {
		return [ns_returnerror 500 "not miniweb"]
	    }
	}
	set ismw 1
    } elseif {$exists && $isdir} {
	return [ns_returnerror 500 "File is a directory"]
    }

    if {[catch {
	if {$ismw} {

	    #
	    # Create MiniWeb directory if necessary.
	    #

	    set dir $file
	    if {!$exists} {
		ns_mkdir $dir
	    }

	    #
	    # Spool MiniWeb content in a tmp file.
	    #

	    set tmp [ns_tmpnam]
	    set fp [open $tmp w+]
	    fconfigure $fp -translation binary
	    ns_conncptofp $fp

	    #
	    # Read the headers and save the content
	    # for each included file.
	    #

	    seek $fp 0
	    while {[gets $fp line] >= 0} {
		set line [string trim $line]
		if {![string length $line]} {
		    if {![info exists length] ||
			![info exists name]} {
			close $fp
			return [ns_returnbadrequest]
		    }
		    set file $dir/$name
		    set tmp2 [ns_mktemp $file.putXXXXXX]
		    set fp2 [open $tmp2 w]
		    ns_cpfp $fp $fp2 $length
		    close $fp2
		    ns_rename $tmp2 $file
		    unset name
		    unset length
		} else {
		    set kv [split $line :]
		    set k [string trim [lindex $kv 0]]
		    set v [string trim [lindex $kv 1]]
		    switch [string tolower $k] {
			content-name {
			    set name $v
			}
			content-length {
			    set length $v
			}
		    }
		}
	    }
	    close $fp
	    ns_unlink $tmp
	} else {

	    #
	    # Save an ordinary file.
	    #

	    set tmp [ns_mktemp $file.putXXXXXX]
 	    if {[catch {open $tmp w} res]} {
 		if {![nsv_get _ns_fastpath builddirs]} {
 		    error $res
 		}
 		###
 		# We can accomodate the case where an implied directory tree
 		#  needs to be constructed.
 		###
		set fpath [file dirname $tmp]
		if {![file isdirectory $fpath]} {
		    file mkdir $fpath
		    # Now, retry the open; allow error propogation
		    set fp [open $tmp w]
		} else {
		    error $res
		}
 	    } else {
 		set fp $res
 	    }
	    ns_conncptofp $fp
	    close $fp
	    ns_rename $tmp $file
	}
    } err]} {
	ns_log error "put: save $url failed: $err"
	return [ns_returnerror 500 "Save failed"]
    }
    ns_returnok
}
