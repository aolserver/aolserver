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
# $Header: /Users/dossy/Desktop/cvs/aolserver/nsperm/init.tcl,v 1.3 2000/08/02 23:38:25 kriston Exp $
#

#
# modules/nsperm/init.tcl -
#	Initialization for nsperm module
#

proc init_nsperm { } {
    set dir "[ns_info home]/servers/[ns_info server]/modules/nsperm"

    #
    # Parse hosts.allow
    #
    set filename "$dir/hosts.allow"
    if {[catch {set file [open $filename r]} ignore] == 0} {
	while {![eof $file]} {
	    set line [gets $file]
	    if {[string range $line 0 0] != "#"} {
		if {[string length $line] > 0} {
		    set list [split $line :]
		    if {[llength $list] != 2} {
			ns_log error "init_nsperm: bad line in $filename: $line"
		    } else {
			set user [lindex $list 0]
			set addrs [lindex $list 1]
			foreach addr $addrs {
			    set addr [string trim $addr]
			    lappend _ns_allow($user) $addr
			}
		    }
		}
	    }
	}
	close $file
    }

    #
    # Parse hosts.deny
    #
    set filename "$dir/hosts.deny"
    if {[catch {set file [open $filename r]} ignore] == 0} {
	while {![eof $file]} {
	    set line [gets $file]
	    if {[string range $line 0 0] != "#"} {
		if {[string length $line] > 0} {
		    set list [split $line :]
		    if {[llength $list] != 2} {
			ns_log error "init_nsperm: bad line in $filename: $line"
		    } else {
			set user [lindex $list 0]
			set addrs [lindex $list 1]
			foreach addr $addrs {
			    set addr [string trim $addr]
			    if {[info exists _ns_allow($user)]} {
				ns_log error "init_nsperm: both allow and deny entries exist for user \"$user\""
			    } else {
				lappend _ns_deny($user) $addr
			    }
			}
		    }
		}
	    }
	}
	close $file
    }

    #
    # Parse passwd
    #
    set filename "$dir/passwd"
    if {[catch {set file [open $filename r]} ignore] == 0} {
	while {![eof $file]} {
	    set line [gets $file]
	    if {[string range $line 0 0] != "#"} {
		if {[string length $line] > 0} {
		    set list [split $line :]
		    if {[llength $list] != 7} {
			ns_log error "nsperm_init: bad line in $filename: $line"
		    } else {
			set user [lindex $list 0]
			set pass [lindex $list 1]
			set uf1 [lindex $list 4]
			set cmd "ns_perm adduser $user \"$pass\" \"$uf1\""
			if {[info exists _ns_allow($user)]} {
			    append cmd " -allow "
			    foreach a $_ns_allow($user) {
				append cmd " $a"
			    }
			} 
			if {[info exists _ns_deny($user)]} {
			    append cmd " -deny "
			    foreach a $_ns_deny($user) {
				append cmd " $a"
			    }
			}
			eval $cmd
		    }
		}
	    }
	}
	close $file
    }

    #
    # Parse group
    #
    set filename "$dir/group"
    if {[catch {set file [open $filename r]} ignore] == 0} {
	while {![eof $file]} {
	    set line [gets $file]
	    if {[string range $line 0 0] != "#"} {
		if {[string length $line] > 0} {
		    set list [split $line :]
		    if {[llength $list] != 4} {
			ns_log error "nsperm_init: bad line in $filename: $line"
		    } else {
			set group [lindex $list 0]
			set users [split [lindex $list 3] ,]
			set cmd "ns_perm addgroup $group"
			foreach user $users {
			    set user [string trim $user]
			    append cmd " $user"
			}
			eval $cmd
		    }
		}
	    }
	}
	close $file
    }

    #
    # Parse perms
    #
    set filename "$dir/perms"
    if {[catch {set file [open $filename r]} ignore] == 0} {
	while {![eof $file]} {
	    set line [gets $file]
	    if {[string range $line 0 0] != "#"} {
		if {[string length $line] > 0} {
		    if {[llength $line] != 5} {
			ns_log error "nsperm_init: bad line in $filename: $line"
		    } else {
			set action [lindex $line 0]
			set inherit [lindex $line 1]
			set method [lindex $line 2]
			set url [lindex $line 3]
			set entity [lindex $line 4]
			set cmd "ns_perm $action"
			if {$inherit == "noinherit"} {
			    append cmd " -noinherit"
			}
			append cmd " $method $url \"$entity\""
			eval $cmd
		    }
		}
	    }
	}
	close $file
    }
}

#
# ns_permpasswd lets you set a password in the nsperm passwd file.
# It is implemented in tcl because the passwd file is no inherently a
# part of the nsperm module--just a nice interface provided by the
# supporting Tcl code.
#
# oldpass must either be the user's old password or nsadmin's password
# for the action to succeed.
#

proc ns_permpasswd { targetuser oldpass newpass } {
    set dir "[ns_info home]/servers/[ns_info server]/modules/nsperm"
    set filename "$dir/passwd"
    set file [open $filename r]
    set oldfile ""

    #
    # Verify that this is an allowed action
    #

    if {[catch {ns_perm checkpass $targetuser $oldpass} ignore] != 0} {
	if {[catch {ns_perm checkpass nsadmin $oldpass} ignore] != 0} {
	    return "incorrect old password"
	}
    }
    
    while {![eof $file]} {
	set line [gets $file]
	set aline $line
	if {[string range $line 0 0] != "#"} {
	    if {[string length $line] > 0} {
		set list [split $line :]
		if {[llength $list] != 7} {
		    ns_log error "ns_permpassword: bad line in $filename: $line"
		} else {
		    set user [lindex $list 0]
		    if {$user == $targetuser} {
			set aline "[lindex $list 0]:[ns_crypt $newpass CU]:[lindex $list 2]:[lindex $list 3]:[lindex $list 4]:[lindex $list 5]:[lindex $list 6]"
		    }
		}
	    }
	}
	lappend oldfile $aline
    }
    close $file

    set file [open $filename w]
    foreach l $oldfile {
	puts $file $l
    }
    close $file

    ns_perm setpass $targetuser [ns_crypt $newpass CU]
    return ""
}


#
# Initialize the module
#

init_nsperm

