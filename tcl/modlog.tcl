#
# The contents of this file are subject to the AOLserver Public License
# Version 1.1 (the "License"); you may not use this file except in
# compliance with the License. You may obtain a copy of the License at
# http://aolserver.lcs.mit.edu/.
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
# $Header: /Users/dossy/Desktop/cvs/aolserver/tcl/modlog.tcl,v 1.1.1.1 2000/05/02 13:48:25 kriston Exp $
#

#
# this scripts gets the realm and their corresponding logfiles from
# "ns/servers/server1/realms" section in nsd.tcl
#
# example:
#
#    ns_section "ns/servers/server1/realms"
#    ns_param realm1 "/usr/tmp/realm1.out"
#    ns_param realm2 "/usr/tmp/realm2.out"
#

catch {
    set section [ns_configsection "ns/servers/server1/realms"]

    set size [ns_set size $section]
    for {set i 0} {$i < $size} {incr i} { 
	set realm [ns_set key $section $i]
	ns_modlogcontrol register $realm
	set logfile [ns_set value $section $i]
	ns_modlogcontrol redirect $realm $logfile
    }
}
