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
# $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/init.tcl,v 1.13 2003/01/23 18:29:53 shmooved Exp $
#

#
# init.tcl --
#
#   Core script to initialize a virtual server at startup.
#

#
# ns_module --
#
#   Set or return information about
#   the currently initializing module.
#   This proc is useful in init files.
#

proc ns_module {key {val ""}} {
    global _module
    switch $key {
        name    -
        private -
        library -
        shared  {
            if {$key == "library"} {
                set key private
            }
            if {$val != ""} {
                set _module($key) $val
            }
            if {[info exists _module($key)]} {
                set val $_module($key)
            }
        }
        clear {
            catch {unset _module}
        }
        default {
            error "ns_module: invalid cmd: $key"
        }
    }
    return $val
}


#
# ns_eval --
#
#   Evaluate a script which should contain
#   new procs commands and then save the
#   state of the procs for other interps
#   to sync with on their next _ns_atalloc.
#
#   If this ever gets moved to a namespace,
#   the eval will need to be modified to
#   ensure that the procs aren't defined 
#   in that namespace.
#

proc ns_eval {args} {
    if {[catch {uplevel $args} result]} {
        ns_markfordelete; # dump this interp to avoid proc pollution.
        return -code error $result
    }
    _ns_savenamespaces; # save this interp's namespaces for others.
    return $result
}


#
# ns_adp_include --
#
#   Wrapper for _ns_adp_include to ensure a
#   new call frame with private variables.
#

proc ns_adp_include {args} {
    eval _ns_adp_include $args
}


#
# ns_init --
#
#   Initialize the interp.  This proc is called
#   by the Ns_TclAllocateInterp C function.
#

proc ns_init {} {
    ns_ictl update; # check for proc/namespace update
}


#
# ns_cleanup --
#
#   Cleanup the interp, performing various garbage
#   collection tasks.  This proc is called by the
#   Ns_TclDeAllocateInterp C function.
#

proc ns_cleanup {} {
    ns_cleanupchans; # close files
    ns_cleanupvars;  # dump globals
    ns_set  cleanup; # free sets
    ns_http cleanup; # abort any http request
    ns_ictl cleanup; # internal cleanup (e.g,. Ns_TclRegisterDefer's)
}


#
# ns_cleanupchans --
#
#   Close all open channels.
#

proc ns_cleanupchans {} {
    ns_chan cleanup; # close shared channels first
    foreach f [file channels] {
        # NB: Leave core Tcl stderr, stdin, stdout open.
        if {![string match std* $f]} {
            catch {close $f}
        }
    }
}


#
# ns_cleanupvars --
#
#   Unset global variables.
#

proc ns_cleanupvars {} {
    foreach g [info globals] {
        switch -glob -- $g {
            auto_* -
            tcl_*  - 
            env {
                # NB: Save these core Tcl vars.
            }
            default {
                upvar \#0 $g gv
                if {[info exists gv]} {
                    unset gv
                }
            }
        }
    }

    global errorInfo errorCode
    set errorInfo ""
    set errorCode ""
}


#
# ns_reinit --
#
#   Cleanup and initialize an interp.
#   This could be used for long running
#   detached threads to avoid resource 
#   leaks and/or missed state changes, e.g.:
#
#   ns_thread begin {
#       while 1 {
#           ns_reinit
#           ... endless work ...
#       }
#   }
#

proc ns_reinit {} {
    ns_cleanup
    ns_init
}


#
# _ns_savenamespaces --
#
#   Save this interp's namespaces
#   as the template for other interps.
#

proc _ns_savenamespaces {} {
    append script [_ns_getpackages] ; # Assure C-level packages load first
    _ns_getnamespaces namespaces
    set import ""
    foreach ns $namespaces {
        foreach {_ns_script _ns_import} [_ns_getscript $ns] {
            break
        }
        append script [list namespace eval $ns $_ns_script] \n
        if {$_ns_import != ""} {
            append import [list namespace eval $ns $_ns_import] \n
        }
    }
    if {$import != ""} {
        append script $import
    }
    ns_ictl save $script
}


#
# _ns_sourcemodule --
#
#   Source files for a module.
#

proc _ns_sourcemodule module {
    set shared  [ns_library shared  $module]
    set private [ns_library private $module]
    ns_module name    $module
    ns_module private $private
    ns_module shared  $private
    _ns_sourcefiles $shared $private
    ns_module clear
}


#
# _ns_sourcefiles --
#
#   Evaluate the files in the shared
#   and private Tcl directories.
#

proc _ns_sourcefiles {shared private} {
    set files ""

    #
    # Append shared files not in private, sourcing
    # init.tcl immediately if it exists.
    #

    foreach file [lsort [glob -nocomplain $shared/*.tcl]] {
        set tail [file tail $file]
        if {$tail == "init.tcl"} {
            _ns_sourcefile $file
        } else {
            if {![file exists $private/$tail]} {
                lappend files $file
            }
        }
    }

    #
    # Append private files, sourcing init.tcl
    # immediately if it exists.
    #

    foreach file [lsort [glob -nocomplain $private/*.tcl]] {
        set tail [file tail $file]
        if {$tail == "init.tcl"} {
            _ns_sourcefile $file
        } else {
            lappend files $file
        }
    }

    #
    # Source list of files.
    #

    foreach file $files {
        _ns_sourcefile $file
    }
}


#
# _ns_sourcefile --
#
#   Source a script file.
#

proc _ns_sourcefile {file} {
    if {[catch {source $file} err]} {
        ns_log error "tcl: source $file failed: $err"
    }
}


#
# _ns_getnamespaces -
#
#   Recursively append the list of all known namespaces
#   to the variable named by listVar variable.
#

proc _ns_getnamespaces {listVar {n ""}} {
    upvar $listVar list
    lappend list $n
    foreach c [namespace children $n] {
        _ns_getnamespaces list $c
    }
}


#
# _ns_getpackages -
#
#   Get list of loaded packages and return script
#   used to re-load each package into new interps.
#   Note the overloading of the Tcl "load" command.
#   This way we assure recursive invocation of the
#   "package require" from within C-level packages.
#

proc _ns_getpackages {} {
    append script [subst {set ::_pkglist [list [info loaded]]}] \n
    append script {
        if {[info commands ::tcl::_load] != ""} {
            rename ::tcl::_load ""
        }
        rename ::load ::tcl::_load
        proc ::load {args} {
            set libfile [lindex $args 0]
            for {set i 0} {$i < [llength $::_pkglist]} {incr i} {
                set toload [lindex $::_pkglist $i]
                if {$libfile == [lindex $toload 0]} {
                    eval ::tcl::_load $toload
                    set ::_pkglist [lreplace $::_pkglist $i $i]
                    return
                }
            }
        }
        while {[llength $::_pkglist]} {
            eval ::load [lindex $::_pkglist 0]
        }
        rename ::load ""
        rename ::tcl::_load ::load
    }
}


#
# _ns_getscript --
#
#   Return a script to create namespaces
#   and their data (vars and procs).
#

proc _ns_getscript n {
    namespace eval $n {

        #
        # Cover namespace variables (arrays and scalars)
        #

        ::set _script ""
        ::foreach _var [::info vars] {
            ::switch -- $_var {
                _var -
                _script {
                    continue ; # skip local help variables
                }
                default {
                    ::if {[::info exists [::namespace current]::$_var]} {
                        ::if {[::array exists $_var]} {
                            # handle array variable
                            ::append _script [::list variable $_var] \n
                            ::append _script \
                                [::list array set $_var [::array get $_var]] \n
                        } else {
                            # handle scalar variable
                            ::append _script \
                                [::list variable $_var [::set $_var]] \n
                        }
                    }
                }
            }
        }
        
        #
        # Cover namespace procedures
        #

        ::set _import ""
        ::foreach _proc [::info procs] {
            ::set _orig [::namespace origin $_proc]
            ::set _args ""
            ::if {$_orig == [::namespace which -command $_proc]} {
                # original command; get the definition
                ::foreach _arg [::info args $_proc] {
                    if {[::info default $_proc $_arg _def]} {
                        ::set _arg [::list $_arg $_def]
                    }
                    ::lappend _args $_arg
                }
                ::append _script \
                    [::list proc $_proc $_args [::info body $_proc]] \n
            } else {
                # command imported from other namespace
                ::append _import [::list namespace import $_orig] \n
            }
        }

        ::append _script [::concat namespace export [::namespace export]] \n
        ::return [::list $_script $_import]
    }
}


#
# Source the top level Tcl libraries.
#

_ns_sourcefiles [ns_library shared] [ns_library private]

#
# Source the module-specific Tcl libraries.
#

foreach module [ns_ictl getmodules] {
    _ns_sourcemodule $module
}

#
# Save the current namespaces.
#

rename _ns_sourcemodule {}
rename _ns_sourcefiles  {}
rename _ns_sourcefile   {}

ns_cleanup
_ns_savenamespaces

#
# Kill this interp to save memory.
#

ns_markfordelete

# EOF $RCSfile: init.tcl,v $
