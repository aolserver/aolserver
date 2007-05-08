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
# $Header: /Users/dossy/Desktop/cvs/aolserver/tcl/init.tcl,v 1.6 2007/05/08 17:23:54 michael_andrews Exp $
#

#
# init.tcl --
#
#	AOLserver looks for init.tcl before sourcing all other files
#	in directory order.
#

#
# Initialize errorCode and errorInfo like tclsh does.
#

set ::errorCode ""
set ::errorInfo ""

#
# Make sure Tcl package loader starts looking for
# packages with our private library directory and not
# in some public, like /usr/local/lib or such. This
# way we avoid clashes with modules having multiple
# versions, one for general use and one for AOLserver.
#

if {![info exists ::auto_path]} {
    set ::auto_path [file join [ns_info home] lib]
} elseif {[lsearch -exact $::auto_path [file join [ns_info home] lib]] == -1} {
    lappend [concat [file join [ns_info home] lib] $::auto_path]
}

# EOF $RCSfile: init.tcl,v $
