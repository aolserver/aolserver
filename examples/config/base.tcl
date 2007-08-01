#
# $Header: /Users/dossy/Desktop/cvs/aolserver/examples/config/base.tcl,v 1.4 2007/08/01 21:35:26 michael_andrews Exp $
# $Name:  $
#
# base.tcl --
#
#     A simple example of a Tcl based AOLserver configuration file.
#
# Results:
#
#     HTTP (nssock):       
#
#         http://<address>:8000/
#
#     Server Page Root:      
#     
#         $AOLSERVER/servers/server1/pages
#
#     Server Access Log:
#
#         $AOLSERVER/servers/server1/modules/nslog/access.log
#
# Notes:
#
#     To start AOLserver, make sure you are in the AOLserver
#     installation directory, usually /usr/local/aolserver, and
#     execute the following command:
#
#     % bin/nsd -ft sample-config.tcl 
#

set home [file dirname [ns_info config]]
set pageRoot $home/servers/server1/pages

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

ns_section "ns/servers"
    ns_param server1 "server1"

ns_section "ns/server/server1"
    ns_param directoryfile "index.htm,index.html,index.adp"
    ns_param pageroot $pageRoot
    ns_param maxthreads 20
    ns_param minthreads 5
    ns_param maxconnections 20
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
    ns_param hostname [ns_info hostname]
    ns_param address [ns_info address]
    ns_param port 8000

ns_section "ns/server/server1/module/nslog"
    ns_param rolllog true
    ns_param rollonsignal true
    ns_param rollhour 0
    ns_param maxbackup 2

ns_section "ns/server/server1/module/nscp"
    ns_param address "127.0.0.1"
    ns_param port 8001
    ns_param cpcmdlogging "false"

ns_section "ns/server/server1/module/nscp/users"
    ns_param user ":"
