#
# $Header: /Users/dossy/Desktop/cvs/aolserver/Attic/sample-config.tcl,v 1.17 2005/08/19 13:41:36 shmooved Exp $
# $Name:  $
#
# sample-config.tcl --
#
#     A simple example of a Tcl based AOLserver configuration 
#     file. The following items will be configured:
#
#     HTTP (nssock):       
#
#         http://<address>:8000/
#
#     AOLserver Stats Web Interface: 
#
#         http://<address>:8000/_stats/ 
#
#         User name: aolserver
#         Password: stats    
#
#     Control Port (nscp): 
#
#         127.0.0.1:8001 
#
#         User name: <empty>
#         Password: <empty>
#
#     Page Root:      
#     
#         $AOLSERVER/servers/server1/pages
#
#     Server Access Log:
#
#         $AOLSERVER/servers/server1/modules/nslog/access.log
#
#     To start AOLserver, make sure you are in the AOLserver
#     installation directory, usually /usr/local/aolserver, and
#     execute the following command:
#
#         bin/nsd -ft sample-config.tcl 
#

set home [file dirname [ns_info config]]
set pageRoot $home/servers/server1/pages

set sockAddress [ns_info address]
set sockPort 8000

set nscpAddress "127.0.0.1"
set nscpPort 8001

ns_section "ns/parameters"
    ns_param home $home
    ns_param logdebug true

ns_section "ns/mimetypes"
    ns_param default "*/*"
    ns_param .adp "text/html; charset=iso-8859-1"

ns_section "ns/encodings"
    ns_param adp "iso8859-1"

ns_section "ns/threads"
    ns_param stacksize [expr 128 * 1024]
    ns_param mutexmeter true

ns_section "ns/servers"
    ns_param server1 "server1"

ns_section "ns/server/stats"
    ns_param enabled true
    ns_param user "aolserver"
    ns_param password "stats"
    ns_param url "/_stats"

ns_section "ns/server/server1"
    ns_param directoryfile "index.htm,index.html,index.adp"
    ns_param pageroot $pageRoot
    ns_param maxthreads 20
    ns_param minthreads 5
    ns_param maxconns 20
    ns_param urlcharset "utf-8"
    ns_param outputcharset "utf-8"
    ns_param inputcharset "utf-8"

ns_section "ns/server/server1/adp"
    ns_param map "/*.adp"

ns_section "ns/server/server1/modules"
    ns_param nssock nssock.so
    ns_param nslog nslog.so
    ns_param nscp nscp.so

ns_section "ns/server/server1/module/nssock"
    ns_param address $sockAddress
    ns_param port $sockPort
    ns_param hostname [ns_info hostname]

ns_section "ns/server/server1/module/nslog"
    ns_param rolllog true
    ns_param rollonsignal true
    ns_param rollhour 0
    ns_param maxbackup 2

ns_section "ns/server/server1/module/nscp"
    ns_param address $nscpAddress
    ns_param port $nscpPort

ns_section "ns/server/server1/module/nscp/users"
    ns_param user ":"
