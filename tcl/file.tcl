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
# $Header: /Users/dossy/Desktop/cvs/aolserver/tcl/file.tcl,v 1.5 2009/01/19 12:21:08 gneumann Exp $
#

#
# file.tcl --
#
#      Support for .tcl-style dynamic pages.
#

#
# Register the ns_sourceproc handler for .tcl files
# if enabled (default: off).
#

set path ns/server/[ns_info server]
set on [ns_config -bool $path enabletclpages off]
ns_log notice "conf: \[$path\]enabletclpages = $on"


if {$on} {
    ns_share errorPage
    ns_log notice "tcl: enabling .tcl pages"
    ns_register_proc GET /*.tcl ns_sourceproc 
    ns_register_proc POST /*.tcl ns_sourceproc
    ns_register_proc HEAD /*.tcl ns_sourceproc
    set errorPage [ns_config "${path}/tcl" errorpage]
}


#
# ns_tcl_abort is a work-alike ns_adp_abort.
#
proc ns_tcl_abort {} {
    error ns_tcl_abort "" NS_TCL_ABORT
}


proc ns_sourceproc {conn args} {

    ns_share errorPage

    set file [ns_url2file [ns_conn url $conn]]
    if {![file exists $file]} {
	ns_returnnotfound $conn
    } else {
	set code [catch {
	    source $file
	} result ]

	global errorCode errorInfo
	
	if { ![info exists errorCode] } {
	    # Tcl bug workaround.
	    set errorCode NONE
	}
	if { ![info exists errorInfo] } {
	    # Another Tcl bug workaround.
	    set errorInfo ""
	}
	
	if {$code == 1 && $errorCode eq "NS_TCL_ABORT"} {
	    return
	}

	if { $errorPage eq "" } {
	    return -code $code \
		-errorcode $errorCode -errorinfo $errorInfo $result
	} else {
	    ## Custom error page -- unfortunately we can't pass parameters.
	    source $errorPage
	}
    }
}
