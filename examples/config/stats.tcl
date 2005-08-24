#
# $Header: /Users/dossy/Desktop/cvs/aolserver/examples/config/stats.tcl,v 1.1 2005/08/24 13:59:38 shmooved Exp $
# $Name:  $
#
# stats.tcl --
#
#     AOLserver Web stats configuration example. 
#
# Results:     
#
#     A Web based stats interface will configured:
#
#     http://<address>:<port>/_stats/
#
#     The user name and password (aolserver/stats) are configured by
#     default. For security reasons, if you are enabling stats 
#     in a production environment, you should change the default 
#     user name and password.
#

ns_section "ns/server/stats"
    ns_param enabled true
    ns_param user "aolserver"
    ns_param password "stats"
    ns_param url "/_stats"

#
# Mutex metering must be enabled in order for lock contention
# statistics to be collected and displayed.
# 

ns_section "ns/threads"
    ns_param mutexmeter true
