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
# $Header: /Users/dossy/Desktop/cvs/aolserver/Attic/sample-config.tcl,v 1.3 2002/10/30 00:41:42 jgdavidson Exp $
#

#
# sample-config.tcl --  Example config script.
#
#       This script is an AOLserver configuration script with
#	several example sections.  To use:
#
#	% cp sample-config.tcl myconfig.tcl
#	% vi myconfig.tcl		(edit as needed)
#	% bin/nsd -ft myconfig.tcl	(test in foreground)
#	% bin/nsd -t myconfig.tcl	(run in background)
#	% gdb bin/nsd
#	(gdb) run -fdt myconfig.tcl	(run in debugger)
#

#
# Set some Tcl variables that are commonly used throughout this file.
#

set httpport               8000
set httpsport              8443

# The hostname and address should be set to actual values.
set hostname               [ns_info hostname]
set address                [ns_info address]

set servername             "server1"
set serverdesc             "Server Name"

set homedir                [file dirname [ns_info config]]
set bindir                 [file dirname [ns_info nsd]]

set pageroot               ${homedir}/servers/${servername}/pages
set directoryfile          index.adp,index.html,index.htm

# nsssl: Only loads if keyfile.pem and certfile.pem exist.
#set sslmodule              nsssl.so  ;# Domestic 128-bit/1024-bit SSL.
set sslmodule              nsssle.so ;# Exportable 40-bit/512-bit SSL.
set sslkeyfile   ${homedir}/servers/${servername}/modules/nsssl/keyfile.pem
set sslcertfile  ${homedir}/servers/${servername}/modules/nsssl/certfile.pem

# nscp: Uncomment the sample password and log in with "nsadmin", password "x",
#       type "ns_crypt newpassword salt" and put the encrypted string below.
set nscp_port 9999
set nscp_addr 127.0.0.1
set nscp_user ""
#set nscp_user "nsadmin:t2GqvvaiIUbF2:" ;# sample user="nsadmin", pw="x".


#
# Global server parameters
#
ns_section "ns/parameters"
ns_param   home            $homedir
ns_param   debug           false

#
# Thread library (nsthread) parameters
#
ns_section "ns/threads"
#ns_param   stacksize [expr 128*1024] ;# Per-thread stack size.

#
# MIME types.
#
#  Note: AOLserver already has an exhaustive list of MIME types, but in
#  case something is missing you can add it here.
#
ns_section "ns/mimetypes"
ns_param   default         "*/*"     ;# MIME type for unknown extension.
ns_param   noextension     "*/*"     ;# MIME type for missing extension.
#ns_param   ".xls"          "application/vnd.ms-excel"


############################################################
#
# Server-level configuration
#
#  There is only one server in AOLserver, but this is helpful when multiple
#  servers share the same configuration file.  This file assumes that only
#  one server is in use so it is set at the top in the "server" Tcl variable.
#  Other host-specific values are set up above as Tcl variables, too.
#

ns_section "ns/servers"
ns_param   $servername     $serverdesc


#
# Server parameters
#
ns_section "ns/server/${servername}"
ns_param   directoryfile   $directoryfile
ns_param   pageroot        $pageroot
ns_param   enabletclpages  false     ;# Parse *.tcl files in pageroot.


#
# Scaling and Tuning Options
#
#  Note: These values aren't necessarily the defaults.
#
#ns_param   connsperthread  0         ;# Normally there's one conn per thread
#ns_param   flushcontent    false     ;# Flush all data before returning
#ns_param   maxconnections  100       ;# Max connections to put on queue
#ns_param   maxdropped      0         ;# Shut down if dropping too many conns
#ns_param   maxthreads      20        ;# Tune this to scale your server
#ns_param   minthreads      0         ;# Tune this to scale your server
#ns_param   threadtimeout   120       ;# Idle threads die at this rate


# Directory listings -- use an ADP or a Tcl proc to generate them.
#ns_param   directoryadp    $pageroot/dirlist.adp ;# Choose one or the other.
#ns_param   directoryproc   _ns_dirlist           ;#  ...but not both!
#ns_param   directorylisting simple               ;# Can be simple or fancy.


#
# ADP (AOLserver Dynamic Page) configuration
#
ns_section "ns/server/${servername}/adp"
ns_param   map             "/*.adp"  ;# Extensions to parse as ADP's.
#ns_param   map             "/*.html" ;# Any extension can be mapped.
ns_param   enableexpire    false     ;# Set "Expires: now" on all ADP's.
ns_param   enabledebug     false     ;# Allow Tclpro debugging with "?debug".

# ADP special pages
#ns_param   errorpage      ${pageroot}/errorpage.adp ;# ADP error page.


#
# Socket driver module (HTTP)  -- nssock
#
ns_section "ns/server/${servername}/module/nssock"
ns_param   port            $httpport
ns_param   hostname        $hostname
ns_param   address         $address


#
# Socket driver module (HTTPS) -- nsssl
#
#  nsssl does not load unless sslkeyfile/sslcertfile exist (above).
#
ns_section "ns/server/${servername}/module/nsssl"
ns_param   port            $httpsport
ns_param   hostname        $hostname
ns_param   address         $address
ns_param   keyfile         $sslkeyfile
ns_param   certfile        $sslcertfile


#
# Control port -- nscp
#
#  nscp does not load unless nscp_user is a valid user.
#
ns_section "ns/server/${servername}/module/nscp"
ns_param   port            $nscp_port
ns_param   address         $nscp_addr

ns_section "ns/server/${servername}/module/nscp/users"
ns_param   user            $nscp_user


#
# Access log -- nslog
#
ns_section "ns/server/${servername}/module/nslog"
ns_param   rolllog         true      ;# Should we roll log?
ns_param   rollonsignal    true      ;# Roll log on SIGHUP.
ns_param   rollhour        0         ;# Time to roll log.
ns_param   maxbackup       5         ;# Max number to keep around when rolling.


#
# CGI interface -- nscgi
#
#  WARNING: These directories must not live under pageroot.
#
ns_section "ns/server/${servername}/module/nscgi"
#ns_param   map "GET  /cgi /usr/local/cgi"     ;# CGI script file dir (GET).
#ns_param   map "POST /cgi /usr/local/cgi"     ;# CGI script file dir (POST).


#
# Modules to load
#
ns_section "ns/server/${servername}/modules"
ns_param   nssock          ${bindir}/nssock.so
ns_param   nslog           ${bindir}/nslog.so
#ns_param   nscgi           ${bindir}/nscgi.so  ;# Map the paths before using.
#ns_param   nsperm          ${bindir}/nsperm.so ;# Edit passwd before using.

#
# nsssl: Only loads if sslcertfile and sslkeyfile exist (see above).
#
if { [file exists $sslcertfile] && [file exists $sslkeyfile] } {
    ns_param nsssl ${bindir}/${sslmodule}
} else {
    ns_log warning "config.tcl: nsssl not loaded -- key/cert files do not exist."
}

#
# nscp: Only loads if nscp_user is set (see above).
#
if { $nscp_user != "" } {

    if {![string match "127.0.0.1" $nscp_addr]} {
	# Anything but 127.0.0.1 is not recommended.
	ns_log warning "config.tcl: nscp listening on ${nscp_addr}:${nscp_port}"
    }
    ns_param nscp ${bindir}/nscp.so

} else {
    ns_log warning "config.tcl: nscp not loaded -- user/password is not set."
}



#
# Example: Host headers based virtual servers.
#
# To enable:
#
# 1. Load comm driver(s) globally.
# 2. Configure drivers as in a virtual server.
# 3. Add a "servers" section to map virtual servers to Host headers.
#
#ns_section ns/modules
#ns_section nssock nssock.so
#
#ns_section ns/module/nssock
#ns_param   port            $httpport
#ns_param   hostname        $hostname
#ns_param   address         $address
# 
#ns_section ns/module/nssock/servers
#ns_param server1 $hostname:$httpport
#


#
# Example:  Multiple connection thread pools.
#
# To enable:
# 
# 1. Define one or more thread pools.
# 2. Configure pools as with the default server pool.
# 3. Map method/URL combinations to the pools
# 
# All unmapped method/URL's will go to the default server pool.
# 
#ns_section ns/server/server1/pools
#ns_section slow "Slow requests here."
#ns_section fast "Fast requests here." 
#
#ns_section ns/server/server1/pool/slow
#ns_param map {POST /slowupload.adp}
#ns_param maxconnections  100       ;# Max connections to put on queue
#ns_param maxdropped      0         ;# Shut down if dropping too many conns
#ns_param maxthreads      20        ;# Tune this to scale your server
#ns_param minthreads      0         ;# Tune this to scale your server
#ns_param threadtimeout   120       ;# Idle threads die at this rate
#
#ns_section ns/server/server1/pool/fast
#ns_param map {GET /faststuff.adp}
#ns_param maxthreads 10
# 
