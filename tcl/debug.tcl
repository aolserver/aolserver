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
# $Header: /Users/dossy/Desktop/cvs/aolserver/tcl/debug.tcl,v 1.4 2002/02/08 07:56:16 hobbs Exp $
#

#
# debug.tcl --
#
#	Support for the TclPro debugger from Scriptics which allows you
#	to step through the scripts in your ADP pages much like an
#	ordinary stepping debugger, e.g., GDB.
#
# For TclPro to work correctly you'll need to:
#
# 1.	Ensure this file and the latest prodebug.tcl from Scriptics
#	is sourced at startup.
#
# 2.	Enable debugging in ADP with:
#
#		[ns/server/<server>/adp]
#		enabledebug=1
#
# 3.	Start the TclPro debugger for remote debugging by selecting
#	"File->New Project..." menu and checking "Remote Debugging".
#	Note the port number.
#
# 4.	In your browser open an ADP page with the debug=<pattern>
#	query data.  The pattern is a glob-style pattern for files
#	you want the debugger to trap on, e.g., to debug foo.adp
#	and any files in may include use:
#
#		http://server/foo.adp?debug=*
#
# Additional Options:
#
#	If your browser is not running on the same host as TclPro
#	you'll need to set the dhost parameter, e.g.,:
#
#		http://server/foo.adp?debug=*&&dhost=myhost
#
#	If the remote debugging port for TclPro is not the default below
#	you'll have to specify the dport parameter, e.g.:
#
#		http://server/foo.adp?debug=*&dport=3232&dhost=myhost
#
#	Remember you can specify a differnt pattern, e.g., to debug
#	the bar.inc file which is ns_adp_include'd by foo.adp use:
#
#		http://server/foo.adp?debug=bar.inc
#
#	Finally, you may want to step through procedures.  To do so
#	TclPro must "instrument" the procedures.  You can either do
#	this using the "View->Procedures..." menu once the debugger
#	is connected and stopped or by specifying the dprocs parameter
#	which is a glob-style pattern of procedure to be instrumented
#	before the debugger stops.  For example, to have all procedures
#	starting with "myprocs" instrumented use:
#
#		http://server/foo.adp?debug=*&dprocs=myprocs*
#

proc ns_adp_debuginit {procs host port} {
    if {![string length $host]} {
	set host [ns_conn peeraddr]
    }
    if {![string length $port]} {
	# NB: Should match that in prodebug.tcl.
	set port 2576
    }
    if {[string length $procs]} {
	set procsfile [ns_tmpnam]
	set fp [open $procsfile w]
	foreach p [info procs $procs] {
	    if {[string match *debug* $p]} continue
	    set args {}
	    foreach a [info args $p] {
		if {[info default $p $a def]} {
		    lappend args [list $a $def]
		} else {
		    lappend args $a
		}
	    }
	    puts $fp [list proc $p $args [info body $p]]
	}
	close $fp
    }
    if {![debugger_init $host $port]} {
	return -code error \
                "debugger_init: could not connect to $host:$port"
    }
    if {[string length $procs]} {
	DbgNub_sourceCmd $procsfile
	ns_unlink $procsfile
    }
}
