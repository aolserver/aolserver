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
# $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/init.tcl,v 1.24 2003/09/11 17:55:49 elizthom Exp $
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
    if {$len == 0} {
        return
    } elseif {$len == 1} {
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
        # server.
        set lock_id [nsv_get _ns_eval_lock [ns_info server]]
        ns_mutex lock $lock_id

        set th_code [catch {
            set tid [ns_thread begin [list _ns_eval $args]]
            ns_thread join $tid
        } th_result]

        ns_mutex unlock $lock_id
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
    global _ns_lazyprocdef
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
    # With lazyprocdef, need to decr our refcnt we had to 
    # the previous epoch
    if { $_ns_lazyprocdef == 1 } { 
        _ns_cleanupprocs [ expr [ ns_ictl epoch ] - 1 ] 1
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

    set code [catch {uplevel 1 $args} result]

    if $didsaveproc {
	rename ns_eval ""
	rename _saved_ns_eval ns_eval
    }
    return -code $code $result
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
    global _ns_lazyprocdef

    ns_ictl update; # check for proc/namespace update
    if { $_ns_lazyprocdef == 1} {
        _ns_lzproc_refcnt
    }
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
            _ns_ictl_currentepoch -
            _ns_lzproc_loaded -
            _ns_lazyprocdef {
               # needed across cleanups for lzproc processing
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
    set script [_ns_getpackages]
    set import ""
    _ns_getnamespaces nslist
    foreach n $nslist {
        if {  [ns_config -bool ns/parameters lazyprocdef 0] } {
            _ns_lzproc_propogate $n
        }
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
        ::set _ns_lazyprocdef [::ns_config -bool ns/parameters lazyprocdef 0]
        ::set _ns_ictl_currentepoch [ ::ns_ictl epoch ]
        ::incr _ns_ictl_currentepoch
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
                _ns_lzproc_loaded -
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

        ::foreach _proc [::_ns_tclinfo procs] {
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

                ::if { $_ns_lazyprocdef == 1 } {
                    switch -glob -- $_proc {
                        _ns_tclinfo {
                        }
                        info {
                            if { [namespace current] == "::" } {
                                ::append _script \
                                [::list proc _ns_tclinfo_wrap $_args [::info body $_proc]] \n
                            } else {
                                ::append _script \
                                [::list proc $_proc $_args [::info body $_proc]] \n
                            }
                        }
                        ns_* -
                        _ns_* -
                        unknown {
                            ::append _script \
                            [::list proc $_proc $_args [::info body $_proc]] \n
                        }
                        default {
                            ::_ns_lzproc_store $_proc [::list proc $_proc $_args [::info body $_proc]] [ namespace current ]
                        }
                    }
                } else {
                  ::append _script \
                      [::list proc $_proc $_args [::info body $_proc]] \n
                }
            } else {
                # procedure imported from other namespace
                ::append _import [::list namespace import -force $_orig] \n
            }
        }

        if { $_ns_lazyprocdef == 1 && [ namespace current ] == "::" } {
            ::append _script \
                [::list _ns_lzproc_init] \n
        }

        #
        # Cover commands imported from other namespaces
        #

        ::foreach _cmnd [::_ns_tclinfo commands [::namespace current]::*] {
            ::set _orig [::namespace origin $_cmnd]
            ::if {[::info exists _prcs($_cmnd)] == 0 
                    && $_orig != [::namespace which -command $_cmnd]} {
                ::append _import [::list namespace import -force $_orig] \n
            }
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
# Lazy proc processing
#
# This config parameter controls the proc initialization behavior. When off,
# all procs are defined as part of interp initialization. If on, then the
# proc definitions are stored in a global array and loaded in the interp
# at the time of the first usage. This is most effective for servers with
# a large number of procs that must be available, but only a limited number
# are actually used in any particular thread. This can help speed thread
# initialization time and reduce overall memory consumption.
# 
variable _ns_lazyprocdef [ns_config -bool ns/parameters lazyprocdef 0]

variable _ns_ictl_currentepoch [ ns_ictl epoch ]

if { $_ns_lazyprocdef == 1 } {

    #
    # _ns_lzproc_init
    #
    # Things that need to happen for lazy proc whenever the init script
    # is evaluated, but need to be cognizant of whether this is the first
    # time through or a subsequent update.
    # This handles things like the renaming of 'info'
    # This is expected to run in the init script after all the proc defs
    #
    proc _ns_lzproc_init {} {
       global _ns_lzproc_loaded

       # have we been run in this interp before?
       # (rename fails if we have (already been done))
       if { [catch { rename info _ns_tclinfo } err] == 0 } {
           # first time through
           if { [ _ns_tclinfo proc _ns_tclinfo_wrap ] == "_ns_tclinfo_wrap" } {
               rename _ns_tclinfo_wrap info 
           }
       } else {
           # not first time
           if { [ _ns_tclinfo proc _ns_tclinfo_wrap ] == "_ns_tclinfo_wrap" } {
               rename info {}
               rename _ns_tclinfo_wrap info
           }

           # for lzproc'd procs, we need to remove any procs
           # that were loaded into the interp since they may have been redefined
           foreach { _namesp _proc } $_ns_lzproc_loaded {
               eval namespace eval $_namesp { rename $_proc {{}} } 
           }
       }
       set _ns_lzproc_loaded [ list ]
   
    }

    #
    # _ns_lzproc_nsvname 
    #
    # convenience function for compiling the nsv name
    #
    proc _ns_lzproc_nsvname { nameSpace } {
       global _ns_ictl_currentepoch
       return _ns_lzproc_nsv,$_ns_ictl_currentepoch,$nameSpace
    }

    #
    # _ns_lzproc_load --
    #
    # Load a proc (lazy proc definition) from the global store.
    #

    proc _ns_lzproc_load { procName } {
        global _ns_lzproc_loaded
        set procBaseName [ namespace tail $procName ]

        # we load all procs with this name across all namespaces
        # (since we will no longer be able to count on 'unknown' kicking in)
        _ns_getnamespaces nsList
        foreach namesp $nsList {
            if { [ nsv_exists [ _ns_lzproc_nsvname $namesp ] $procBaseName ] &&
                 [ _ns_tclinfo proc ${namesp}::$procBaseName ] == "" } {
                eval namespace eval $namesp { [ nsv_get [ _ns_lzproc_nsvname $namesp ] $procBaseName ] }
                lappend _ns_lzproc_loaded $namesp $procBaseName
            }
        }
        # did we end up with a valid one for the current context?
        if { [ uplevel 2 _ns_tclinfo proc $procName ] == "" } {
            set loaded 0
        } else {
            set loaded 1
        } 
        return $loaded
    }

    #
    # Store the proc in the global store
    # We incr the epoch count id since we are storing
    # in preparation for a new epoch
    #
    proc _ns_lzproc_store { procName procBody nmspace } {
        set procBaseName [ namespace tail $procName ]
        if { $procName == $procBaseName } {
            set ns $nmspace
        } else {
            set ns [ namespace qualifiers $procName ]
            if { $ns == "" } {
                set ns "::"
            }
        }
        set ictlNum [ ns_ictl epoch ]
        incr ictlNum
        nsv_set _ns_lzproc_nsv,$ictlNum,$ns $procBaseName $procBody 
    }

    #
    # _ns_lzproc_propogate -
    #
    # In preparation for a new epoch, we copy all existing defs to
    # the next array (the old one will be cleaned up when its no longer referenced)
    #
    proc _ns_lzproc_propogate { namespace } {
        set currentEpoch [ ns_ictl epoch ]
        set nextEpoch [ expr $currentEpoch + 1 ]
        nsv_array set _ns_lzproc_nsv,$nextEpoch,$namespace [ nsv_array get _ns_lzproc_nsv,$currentEpoch,$namespace ]
    }

    # 
    # _ns_lzproc_refcnt
    #
    # Keep track of references to the lazy proc list so we can cleanup
    # when its no longer needed (i.e. after ns_evals change the current defs)
    #
    proc _ns_lzproc_refcnt {} { 
        global _ns_ictl_currentepoch
        nsv_incr [ _ns_lzproc_nsvname REFCNT ] [ns_thread getid ]
    }

    #
    # _ns_cleanupprocs
    #
    # This gets called on interp cleanup, but does not necessarily
    # indicate that the interp is done with the lzproc array. We know
    # it is done when a) the epoch has changed or b) deleteRef is set
    # (the thread is exiting)
    #
    ns_ictl oncleanup "_ns_cleanupprocs"
    ns_ictl ondelete "_ns_cleanupprocs 0 0 1"

    proc _ns_cleanupprocs { { currentepoch 0 } { decrRefCnt 1 } { deleteRef 0 }} {
       global _ns_ictl_currentepoch
       if { $currentepoch == 0 } {
           set currentepoch $_ns_ictl_currentepoch
       }

       if { ![nsv_exists _ns_lzproc_nsv,$currentepoch,REFCNT [ns_thread getid]]} {
           set _refCnt 0
           set _refExists 0
       } else {
           set _refCnt [ nsv_get _ns_lzproc_nsv,$currentepoch,REFCNT [ns_thread getid]]
           set _refExists 1
       }

       # Decrement the ref count (deleteRef is set to 1 when we're cleaning on thread delete)
       if { $deleteRef || $_refCnt <= [ expr 0 + $decrRefCnt ]  } {
           if { $_refExists } {
               nsv_unset _ns_lzproc_nsv,$currentepoch,REFCNT [ns_thread getid] 
           }
       } else {
           nsv_set _ns_lzproc_nsv,$currentepoch,REFCNT [ns_thread getid] [expr $_refCnt - $decrRefCnt]
       }
 
       # Can we clean up no-longer-needed lzproc defs?
       if { $currentepoch < [ns_ictl epoch] } {
           set _nsvCleanup 1
           foreach _thread [ nsv_array names _ns_lzproc_nsv,$currentepoch,REFCNT ] {
              if { [ nsv_get _ns_lzproc_nsv,$currentepoch,REFCNT $_thread ] > 0 } {
                  set _nsvCleanup 0
                  break
              }
           }
           if { $_nsvCleanup } {
               foreach _nsv [ nsv_names _ns_lzproc_nsv,$currentepoch* ] {
                   nsv_unset $_nsv
               }
           }
       }
    }

    #
    # _ns_lzproc_lookup
    #
    # used by our 'info' wrapper to lookup procs matching the given search pattern
    #
    proc _ns_lzproc_lookup { procPattern currentNs } {
        set cmdList [ list ]
        set patternBase [ namespace tail $procPattern ]
        if { $procPattern == $patternBase } {
            set ns $currentNs
            set parentns $ns
            set parentnsList [ list $ns ]
            while { $parentns != "::" } {
               if { [ catch { set parentns [ namespace parent $parentns ] } err ] } {
                  break
               } else {
                  lappend parentnsList $parentns
               }
            }

            foreach namesp $parentnsList {
                set cmdList [ concat $cmdList [ nsv_array names [ _ns_lzproc_nsvname $namesp ] $patternBase ]]
            }
        } else {
            set ns [ namespace qualifiers $procPattern ]
            if { $ns == "" } {
                set ns "::"
            }
            set tmpList [ nsv_array names [ _ns_lzproc_nsvname $ns ]  $patternBase ]
            foreach cmd $tmpList {
                lappend cmdList ${ns}::${cmd}
            }
            
        }
        return $cmdList
    }


    #
    # unknown --
    #
    # If the config parameter "lazyprocdef" is defined, we wrap the
    # tcl 'unknown' proc with our own which will first attempt to 
    # retrieve the proc from our global store
    #

 
    rename unknown _ns_tclunknown 

    #
    # Look for an unknown proc in our nsv - if its there, load it in and execute it
    # else we go to the tcl unknown processing...
    #
    proc unknown { args } {
        set _proc [ lindex $args 0  ]
        if { [ _ns_lzproc_load $_proc ] } {
            set arglist [lrange $args 1 end]
            return [ uplevel 1 $args ]
        } else {
            return [ uplevel 1 _ns_tclunknown $args ]
        }
    
    }


    #
    # info --
    #
    # In the case of lazy proc definition, we need a wrapper around the tcl 'info' 
    # command to include the procs not yet loaded
    # For 'info commands', we return a sum of what is in our global proc store
    # and what is returned by tcl (removing duplicates). For all other proc-related commands
    # we will load that proc and then run the tcl info command.
    #
    #
    

    # gets renamed to 'info' in the init script
    proc _ns_tclinfo_wrap { args } {
        set _opt [ lindex $args 0 ]
        switch -glob -- $_opt {
            comm* -
            pr*  {
                set _pattern [ lindex $args 1 ]
                if { $_pattern == "" } {
                    set _pattern *
                }
                set _lzcmdlist [ _ns_lzproc_lookup $_pattern [ uplevel 1 namespace current ] ]
                set _cmdlist [ _ns_tclinfo $_opt $_pattern ]
                foreach _cmd $_cmdlist {
                   set _index  [ lsearch -exact $_lzcmdlist $_cmd ]
                   if { $_index == -1 } {
                       lappend _lzcmdlist $_cmd
                   }
                }
                return $_lzcmdlist
             } 
             a* -
             b* -
             c* -
             d* {
                 set _proc [ lindex $args 1 ]
                 if  { [ _ns_tclinfo proc $_proc ] == "" } {
                     _ns_lzproc_load $_proc 
                 } 
                 return [ uplevel 1 _ns_tclinfo $args ]
             }       
             default {
                 return [ uplevel 1 _ns_tclinfo $args ]
             }
        }
    }
} 
 
# wrapper to allow calls within this file that must call the
# tcl info command whether or not lazyprocdef is on
# gets replaced by rename of 'info' when lazyprocdef is on
proc _ns_tclinfo { args } {
    return [ uplevel 1 ::info $args ]
}

#
# _ns_initshutdowncb -
#
#   This is a ns_atshutdown callback which takes care of
#   cleanup of pieces that are placed into global structures
#   at server startup by code in this file.
#

proc _ns_initshutdowncb {} {
    if [nsv_exists _ns_eval_lock [ns_info server]] {
        ns_mutex destroy [nsv_get _ns_eval_lock [ns_info server]]
        nsv_unset _ns_eval_lock [ns_info server]
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
# Create a mutex for ns_eval's serialization
#
if {![nsv_exists _ns_eval_lock [ns_info server]]} {
    nsv_set _ns_eval_lock [ns_info server] [ns_mutex create]
    # Make sure that we cleanup after ourselves
    ns_atshutdown _ns_initshutdowncb
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
