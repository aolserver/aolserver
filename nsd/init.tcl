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
# $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/init.tcl,v 1.6 2002/05/15 20:10:14 jgdavidson Exp $
#

#
# init.tcl --
#
#	Core procedures sourced by each new interp required
#	to initialize a virtual server, share namespaces, and
#	perform garbage collection.
#


#
# ns_module --
#
#	Set or return information about the 
#	currently initializing module.  This 
#	proc is useful in init files.
#

proc ns_module {key {val ""}} {
    global _module

    switch $key {
	name -
	private -
	library -
	shared {
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
#	Evaluate a script which should contain
#	new procs command and then save the
#	state of the procs for other interps
#	to sync with on their next ns_cleanup.
#
#	If this ever gets moved to a namespace, the eval will need
#	to be modified to ensure that the procs aren't defined in
#	that namespace.
#

proc ns_eval script {
    if {[catch {eval $script}]} {
	#
	# If the script failed, just dump this
	# interp to avoid proc pollution.
	# 

	ns_markfordelete
    } else {
	#
	# Save this interp's namespaces for others.
	#

	_ns_savenamespaces
    }
}


#
# ns_cleanup --
#
#	Cleanup the interp, performing various garbage
#	collection tasks and updating the procs if
#	necessary.
#

proc ns_cleanup {} {
    if {[catch {
	_ns_unsetglobals
	# NB: Must be before _ns_closechannels.
	ns_chan cleanup
	_ns_closechannels
	ns_set cleanup
	ns_http cleanup
	_ns_updatenamespaces
    }]} {
	global errorInfo

	#
	# If cleanup failed, dump this interp to
	# avoid an unknown state.
	#

	ns_log error $errorInfo
	ns_markfordelete
    }
}


#
# _ns_unsetglobals --
#
#	Free all but a few well known global variables.
#

proc _ns_unsetglobals {} {
    foreach g [info globals] {
	switch -glob -- $g {
	    tcl* -
	    error -
	    _ns -
	    env {
		# NB: Save these vars.
	    }
	    default {
	    	upvar #0 $g gv
           	if {[info exists gv]} {
               	    unset gv
		}
           }
	}
    }
}


#
# _ns_closechannels --
#
#	Close any remaining open channels.
#

proc _ns_closechannels {} {
    foreach f [file channels] {
	if {![string match std* $f]} {
	    catch {close $f}
	}
    }
}


#
# _ns_updatenamespaces --
#
#	Update namespaces to latest state if necessary.
#

proc _ns_updatenamespaces {} {
    global _ns

    if {![info exists _ns(epoch)]} {
	set _ns(epoch) 0
    }
    if {$_ns(epoch) == [nsv_get _ns epoch]} {
	return
    }
    set lock [nsv_get _ns lock]
    ns_mutex lock $lock
    set current [nsv_get _ns epoch]
    set script [nsv_get _ns script]
    ns_mutex unlock $lock
    eval $script
    set _ns(epoch) $current
}


#
# _ns_savenamespaces --
#
#	Save this interp's namespaces as the template
#	for other interps.
#

proc _ns_savenamespaces {} {
    global _ns

    _ns_getnamespaces namespaces
    set script ""
    foreach ns $namespaces {
	append script [list namespace eval $ns [_ns_getscript $ns]]\n
    }
    set lock [nsv_get _ns lock]
    ns_mutex lock $lock
    nsv_set _ns script $script
    if {[nsv_incr _ns epoch] < 1} {
	nsv_set _ns epoch 1
    }
    set epoch [nsv_get _ns epoch]
    ns_mutex unlock $lock
    set _ns(epoch) $epoch
}


#
# _ns_initserver --
#
#	Init routine for a virtual server.  Should only
#	be called at startup by the main thread.
#

proc _ns_initserver {} {
    #
    # Source the top level Tcl libraries.
    #

    set shared	[ns_library shared]
    set private [ns_library private]
    _ns_sourcefiles $shared $private

    #
    # Source the nsdb Tcl libraries if configured.
    #

    _ns_sourcemodule nsdb 

    #
    # Source the module-specific Tcl libraries.
    #

    set server [ns_info server]
    set mods [ns_configsection ns/server/$server/modules]
    if {$mods != ""} {
    	for {set i 0} {$i < [ns_set size $mods]} {incr i} {
	    _ns_sourcemodule [ns_set key $mods $i]
	}
    }

    #
    # Save the current namespaces.
    #

    _ns_savenamespaces

    #
    # Kill this interp to save memory.
    #

    ns_markfordelete
}

#
# _ns_sourcemodule --
#
#	Source files for a module.
#

proc _ns_sourcemodule module {
    set shared  [ns_library shared $module]
    set private [ns_library private $module]
    ns_module name $module
    ns_module private $private
    ns_module shared $private
    _ns_sourcefiles $shared $private
    ns_module clear
}

#
# _ns_sourcefiles --
#
#	Evaluate the files in the shared and private
#	Tcl directories.
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
#	Source a script file.
#

proc _ns_sourcefile file {
    if {[catch {source $file} err]} {
	ns_log error "tcl: source $file failed: $err"
    }
}


#
# _ns_getnamespaces -
#
#	Recursively append the list of all known namespaces
#	to the variable named by listVar variable.
#

proc _ns_getnamespaces {listVar {n ""}} {
    upvar $listVar list
    lappend list $n
    foreach c [namespace children $n] {
	_ns_getnamespaces list $c
    }
}


#
# _ns_getscript --
#
#	Return a script to create namespace procs.
#

proc _ns_getscript n {
    namespace eval $n {
	::set n [namespace current]
	::set script ""
	::foreach v [::info vars] {
	    ::switch $v {
		n -
		v -
		script continue
		default {
		    ::if {[info exists ${n}::$v]} {
			::if {[array exists $v]} {
			    ::append script [::list variable $v]\n
			    ::append script [::list array set $v [array get $v]]\n
			} else {
			    ::append script [::list variable $v [set $v]]\n
			}
		    }
		}
	    }
	}
	::foreach p [::info procs] {
	    ::set args ""
	    ::foreach a [::info args $p] {
		if {[::info default $p $a def]} {
		    ::set a [::list $a $def]
		}
		::lappend args $a
	    }
	    ::append script [::list proc $p $args [info body $p]]\n
	}
	::append script [::concat namespace export [namespace export]]\n
	::return $script
    }
}





if {[nsv_exists _ns lock]} {
    _ns_updatenamespaces
} else {
    nsv_set _ns lock [ns_mutex create]
    nsv_set _ns epoch 0
    nsv_set _ns script {}
}

