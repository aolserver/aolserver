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
# $Header: /Users/dossy/Desktop/cvs/aolserver/tcl/packages.tcl,v 1.8 2009/01/19 12:21:08 gneumann Exp $
#

set section "ns/server/[ns_info server]/packages"

if {![string length [set nsSetId [ns_configsection $section]]]} {
    return
}

#
# Create a libraryList and a requireList
#
for {set x 0} {$x < [ns_set size $nsSetId]} {incr x} {
    set key [ns_set key $nsSetId $x]
    set value [ns_set value $nsSetId $x]

    if {"library" eq $key} {
        lappend libraryList $value
    } elseif {"require" eq $key} {
        lappend requireList $value
    }
}

ns_set free $nsSetId

#
# Add libraries to ::auto_path var
#
if {[info exists libraryList]} {
    foreach lib $libraryList {
        if {[lsearch -exact $::auto_path $lib] == -1} {
            lappend ::auto_path $lib
            ns_log debug "added library to ::auto_path: ${lib}"
        }
    }
}

#
# Require packages
#
if {![info exists requireList]} {
   return
}

foreach package $requireList { 
    if {[catch {set version [ns_ictl package require $package]}]} {
        ns_log error $::errorInfo
        continue
    }

    ns_log debug "loaded package: ${package}: ${version}"
}

#
# Run init commands
#
foreach command [list nsinit nspostinit] {
    foreach package $requireList {
        set packageCommand "::${package}::${command}"
 
        if {![llength [info commands $packageCommand]]} {
            continue
        }

        ns_log debug "Running: ${packageCommand}"

        if {[catch {eval $packageCommand}]} {
            ns_log error $::errorInfo
        }
    }
}
