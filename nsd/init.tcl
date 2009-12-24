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
# $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/init.tcl,v 1.34 2009/12/24 19:50:34 dvrsn Exp $
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
#   If this is called from within interp init
#   processing, it will devolve to an eval.
#
#   If this ever gets moved to a namespace,
#   the eval will need to be modified to
#   ensure that the procs aren't defined 
#   in that namespace.
#

proc ns_eval {args} {
    set len [llength $args]
    set sync 0
    if {$len == 0} {
        return
    }
    if {$len > 1 && [string match "-sync" [lindex $args 0]]} {
	set sync 1
	set args [lreplace $args 0 0]
	incr len -1
    } elseif {[string match "-pending" [lindex $args 0]]} {
	if {$len != 1} {
	    error "ns_eval: command arguments not allowed with -pending"
	}
	set jlist [ns_job joblist [nsv_get _ns_eval_jobq [ns_info server]]]
	set res [list]
	foreach job $jlist {
	    array set jstate $job
	    set scr $jstate(script)
	    # Strip off the constant, non-user supplied cruft
	    set scr [lindex $scr 1]
	    set stime $jstate(starttime)
	    lappend res [list $stime $scr]
	}
	return $res
    }
    if {$len == 1} {
        set args [lindex $args 0]
    }

    # Need to always incorporate given script into current interp
    # Use this also to verify the script prior to doing the fold into
    # the ictl environment
    set code [catch {uplevel 1 _ns_helper_eval $args} result]
    if {!$code && [ns_ictl epoch]} {
        # If the local eval result was ok (code == 0),
        # and if we are not in interp init processing (epoch != 0),
        # eval the args in a fresh thread to obtain a pristine
        # environment.
        # Note that running the _ns_eval must be serialized for this
        # server.  We are handling this by establishing that the
	# ns_job queue handling these requests will run only a single
	# thread.
	set qid [nsv_get _ns_eval_jobq [ns_info server]]
	set scr [list _ns_eval $args]
	if {$sync} {
	    set th_code [catch {
		set job_id [ns_job queue $qid $scr]
		ns_job wait $qid $job_id
		     } th_result]
	} else {
	    set th_code [catch {ns_job queue -detached $qid $scr} th_result]
	}
	if {$th_code} {
	    return -code $th_code $th_result
	}

    } elseif {$code == 1} {
        ns_markfordelete
    }
    return -code $code $result
}

#
# _ns_eval --
#
#   Internal helper func for ns_eval.  This
#   function will evaluate the given args (from
#   a pristine thread/interp that ns_eval put
#   it into) and then load the result into
#   the interp init script.
#

proc _ns_eval {args} {
    set len [llength $args]
    if {$len == 0} {
        return
    } elseif {$len == 1} {
        set args [lindex $args 0]
    }

    set code [catch {uplevel 1 _ns_helper_eval $args} result]

    if {$code == 1} {
        # TCL_ERROR: Dump this interp to avoid proc pollution.
        ns_markfordelete
    } else {
        # Save this interp's namespaces for others.
        _ns_savenamespaces
    }
    return -code $code $result
}

#
# _ns_helper_eval --
#
#   This Internal helper func is used by both ns_eval and _ns_eval.
#   It will insure that any references to ns_eval from code
#   eval'ed is properly turned into simple evals.
#

proc _ns_helper_eval {args} {
    set didsaveproc 0
    if {[info proc _saved_ns_eval] == ""} {
	rename ns_eval _saved_ns_eval
	proc ns_eval {args} {
            set len [llength $args]
            if {$len == 0} {
                return
            } elseif {$len == 1} {
                set args [lindex $args 0]
            }
	    uplevel 1 $args
	}
	set didsaveproc 1
    }

    set code [catch {uplevel 1 [eval concat $args]} result]

    if {$didsaveproc} {
	rename ns_eval ""
	rename _saved_ns_eval ns_eval
    }
    return -code $code $result
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
    ns_ictl runtraces deallocate
    ns_ictl runtraces allocate
}

#
# _ns_savenamespaces --
#
#   Save this interp's namespaces
#   as the template for other interps.
#

proc _ns_savenamespaces {} {
    set script [_ns_getpackages]
    set import ""
    _ns_getnamespaces nslist
    foreach n $nslist {
        foreach {ns_script ns_import} [_ns_getscript $n] {
            append script [list namespace eval $n $ns_script] \n
            if {$ns_import != ""} {
                append import [list namespace eval $n $ns_import] \n
            }
        }
    }
    ns_ictl save [append script \n $import]
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
    # Append private files, sourcing 
    # init.tcl immediately if it exists.
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
        global errorInfo errorCode
        ns_log error "tcl: source $file failed: $err\n$errorCode\n$errorInfo"
    }
}


#
# _ns_getnamespaces -
#
#   Recursively append the list of all known namespaces
#   to the variable named by listVar variable.
#

proc _ns_getnamespaces {listVar {top "::"}} {
    upvar $listVar list
    lappend list $top
    foreach c [namespace children $top] {
        _ns_getnamespaces list $c
    }
}

#
# _ns_genensemble -
#
#  if the passed command is an ensemble, returns a command 
#  to recreate it
#  otherwise, returns an empty string
#

if {[catch {package require Tcl 8.5}]} {
    proc _ns_getensemble {cmd} {}
} else {
    proc _ns_getensemble {cmd} {
        ::if {[::namespace ensemble exists $cmd]} {
            ::array set _cfg [::namespace ensemble configure $cmd]
            ::set _enns $_cfg(-namespace)
            ::unset _cfg(-namespace)
            ::set _encmd [::list namespace ensemble create -command $cmd {*}[::array get _cfg]]
            return [::list namespace eval $_enns $_encmd]\n
        }
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
        if {[::info commands ::tcl::_load] != ""} {
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
        ::set _script "" ; # script to initialize new interp
        ::set _import "" ; # script to import foreign commands

        #
        # Cover namespace variables (arrays and scalars)
        #

        ::foreach _var [::info vars] {
            ::switch -- $_var {
                _var -
                _import -
                env -
                _script {
                    continue ; # skip local help variables
                               # env should also be skipped as redundant.
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

        ::foreach _proc [::info procs] {
            ::set _orig [::namespace origin $_proc]
            ::set _args ""
            ::set _prcs($_proc) 1
            ::if {$_orig == [::namespace which -command $_proc]} {
                # original procedure; get the definition
                ::foreach _arg [::info args $_proc] {
                    if {[::info default $_proc $_arg _def]} {
                        ::set _arg [::list $_arg $_def]
                    }
                    ::lappend _args $_arg
                }
                ::append _script \
                      [::list proc $_proc $_args [::info body $_proc]] \n
            } else {
                # procedure imported from other namespace
                ::append _import [::list namespace import -force $_orig] \n
            }
        }

        #
        # Cover commands imported from other namespaces
        #

        ::foreach _cmnd [::info commands [::namespace current]::*] {
            ::set _orig [::namespace origin $_cmnd]
            ::if {[::info exists _prcs($_cmnd)] == 0 
                    && $_orig != [::namespace which -command $_cmnd]} {
                ::append _import [::list namespace import -force $_orig] \n
            }
	    ::append _import [_ns_getensemble $_cmnd]
        }


        #
        # Cover commands exported from this namespace
        #

        ::set _exp [::namespace export]
        if {[::llength $_exp]} {
            ::append _script [::concat namespace export $_exp] \n
        }

        ::return [::list $_script $_import]
    }
}


#
# _ns_tclrename -
#
#   Wrapper function to augment the standard Tcl rename
#   command for use by Tcl libraries run from this init module.
#   Because the _ns_savenamespaces function captures only
#   definitions of Tcl defined entities such as 'procs', vars,
#   and namespaces into the ns_ictl Interp Init
#   script, any renaming of non-procs, e.g., core Tcl
#   commands or C-based commands, that are specified from
#   the sourcing of the Tcl libraries do not get captured into
#   the Init script.
#   This rename Wrapper function remedies that, by recording
#   rename actions against non-procs as 'ns_ictl oninit' actions
#   which are executed as part of interp initialization.
#   This Wrapper function is designed to be put into place
#   prior to sourcing the Tcl libraries, and is to be removed
#   once the library sourcing is completed.  It assumes that
#   the native Tcl rename is accessed as '_rename', as established
#   by the sequence:
#     rename rename _rename
#     _rename _ns_tclrename rename
#   
#

proc _ns_tclrename { oldName newName } {
    set is_proc [info procs $oldName]
    if {[catch {_rename $oldName $newName} err]} {
	return -code error -errorinfo $err
    } else {
	if {$is_proc == ""} {
	    ns_ictl oninit "rename $oldName $newName"
	}
    }
}


#
# Source the top level Tcl libraries.
#

#
# Temporarily intercept renames.
#
rename rename _rename
_rename _ns_tclrename rename

_ns_sourcefiles [ns_library shared] [ns_library private]

#
# Source the module-specific Tcl libraries.
#

foreach module [ns_ictl getmodules] {
    _ns_sourcemodule $module
}

#
# Revert to the standard rename defn
#
_rename rename _ns_tclrename
_rename _rename rename

#
# Create a job queue for ns_eval processing
#  This queue must be a single-server queue; we don't
#  want to be processing multiple ns_eval requests
#  simultanously.
#
set srv [ns_info server]
if {![nsv_exists _ns_eval_jobq $srv]} {
    nsv_set _ns_eval_jobq $srv [ns_job create "ns_eval_q:$srv" 1]
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
# Register the init and cleanup callbacks.
#

ns_ictl trace create {ns_ictl update}
ns_ictl trace allocate ns_init
ns_ictl trace deallocate ns_cleanup

#
# Kill this interp to save memory.
#

ns_markfordelete

# EOF $RCSfile: init.tcl,v $
