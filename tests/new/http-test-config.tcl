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
# $Header: /Users/dossy/Desktop/cvs/aolserver/tests/new/http-test-config.tcl,v 1.1 2004/08/25 20:58:31 dossy Exp $
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
set directoryfile          index.adp,index.html,index.htm,index.xhtml,index.xht

set shlibext               [info sharedlibextension]

# nsssl: Only loads if keyfile.pem and certfile.pem exist.
#set sslmodule              nsssl${shlibext}  ;# Domestic 128-bit/1024-bit SSL.
set sslmodule              nsssle${shlibext} ;# Exportable 40-bit/512-bit SSL.
set sslkeyfile   ${homedir}/servers/${servername}/modules/nsssl/keyfile.pem
set sslcertfile  ${homedir}/servers/${servername}/modules/nsssl/certfile.pem

#
# Global server parameters
#
ns_section "ns/parameters"
ns_param   home            $homedir
ns_param   debug           false

#
#         I18N Parameters
#
#ns_param HackContentType false       ;# automatic adjustment of response
                                       # content-type header to include charset
                                       # This defaults to True.
ns_param  OutputCharset  iso-8859-1    ;# Default output charset.  When none specified,
                                       # no character encoding of output is performed.
ns_param  URLCharset     iso-8859-1    ;# Default Charset for Url Encode/Decode.
                                       # When none specified, no character set encoding
                                       # is performed.
#ns_param  PreferredCharsets { utf-8 iso-8859-1 } ;# This parameter supports output
                                       # encoding arbitration.

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

#
#   I18N Mime-types; define content-type header values
#                    to be mapped from these file-types.
#                    Note that you can map file-types of adp files to control
#                    the output encoding through mime-type specificaion.
#                    Remember to add an adp mapping for that extension.
#
ns_param   .adp            "text/html; charset=iso-8859-1"
ns_param   .u_adp          "text/html; charset=UTF-8"
ns_param   .gb_adp         "text/html; charset=GB2312"
ns_param   .sjis_html      "text/html; charset=shift_jis"
ns_param   .sjis_adp       "text/html; charset=shift_jis"
ns_param   .gb_html        "text/html; charset=GB2312"


#
#   I18N File-type to Encoding mappings
#
ns_section "ns/encodings"
ns_param   .utf_html       "utf-8"
ns_param   .sjis_html      "shiftjis"
ns_param   .gb_html        "gb2312"
ns_param   .big5_html      "big5"
ns_param   .euc-cn_html    "euc-cn"
#
# Note: you will need to include file-type to encoding mappings
#       for ANY source files that are to be used, to allow the
#       server to handle them properly.  E.g., the following
#       asserts that the GB-producing .adp files are themselves
#       encoded in GB2312 (this is not simply assumed).
#
ns_param   .gb_adp         "gb2312"


#
# Thread library (nsthread) parameters
#
ns_section "ns/threads"
#ns_param   stacksize [expr 128*1024] ;# Per-thread stack size.


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
#         Server-level I18N Parameters can be specified here, to override
#         the global ones for this server.  These are:
#              HackContentType
#              OutputCharset
#              URLCharset
#         See the global parameter I18N section for a description of these.
#

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

#
# ADP (AOLserver Dynamic Page) configuration
#
ns_section "ns/server/${servername}/adp"
ns_param   map             "/*.adp"  ;# Extensions to parse as ADP's.
#   I18N Note: will need to define I18N specifying mappings of ADP's here as well.
ns_param   map             "/*.u_adp"
ns_param   map             "/*.gb_adp"
ns_param   map             "/*.sjis_adp"
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


# Fast Path --
#
#     Fast path configuration is used to configure options used for serving 
#     static content, and also provides options to automatically display 
#     directory listings.
#
# Parameters:
#                     
#     cache               Boolean. Enable cache for normal URLs.
#                         Optional, default is false.
#     cachemaxsize        Integer. Size of fast path cache.
#                         Optional, default is 5120000.
#     cachemaxentry       Integer. Largest file size allowed in cache.
#                         Optional, default is cachemaxsize / 10.
#     mmap                Boolean. Use mmap() for cache.
#                         Optional, default is false.
#     directoryfile       String. Directory index/default page to
#                         look for. Optional, default is directoryfile
#                         parameter set in ns/server/${servername} section.
#     directorylisting    String. Directory listing style. Optional,
#                         Can be "fancy" or "simple". 
#     directoryproc       String. Name of Tcl proc to use to display
#                         directory listings. Optional, default is to use
#                         _ns_dirlist. You can either specify directoryproc,
#                         or directoryadp - not both.
#     directoryadp        String. Name of ADP page to use to display
#                         directory listings. Optional. You can either
#                         specify directoryadp or directoryproc - not both.
#
# Example:
#
#    ns_section "ns/server/${servername}/fastpath"
#        ns_param directorylisting fancy
#
# See also:
#
#     /aolserver/nsd/fastpath.c
#     /aolserver/tcl/fastpath.tcl         


#
# Example:  Control port configuration.
#
# To enable:
#  
# 1. Define an address and port to listen on. For security
#    reasons listening on any port other then 127.0.0.1 is 
#    not recommended.
#
# 2. Decided whether or not you wish to enable features such
#    as password echoing at login time, and command logging. 
#
# 3. Add a list of authorized users and passwords. The entires
#    take the following format:
#
#    <user>:<encryptedPassword>:
#
#    You can use the ns_crypt Tcl command to generate an encrypted
#    password. The ns_crypt command uses the same algorithm as the 
#    Unix crypt(3) command. You could also use passwords from the
#    /etc/passwd file.
#
#    The first two characters of the password are the salt - they can be 
#    anything since the salt is used to simply introduce disorder into
#    the encoding algorithm.
#
#    ns_crypt <key> <salt>
#    ns_crypt x t2
#    
#    The configuration example below adds the user "nsadmin" with a 
#    password of "x".
#
# 4. Make sure the nscp module is loaded in the modules section.
#
#ns_section "ns/server/${servername}/module/nscp"
#    ns_param address 127.0.0.1        
#    ns_param port 9999
#    ns_param echopassword 1
#    ns_param cpcmdlogging 1
#
#ns_section "ns/server/${servername}/module/nscp/users"
#    ns_param user "nsadmin:t2GqvvaiIUbF2:"
#
#ns_section "ns/server/${servername}/modules"
#    ns_param nscp ${bindir}/nscp${shlibext}
#

#
# Access log -- nslog
#
ns_section "ns/server/${servername}/module/nslog"
ns_param   rolllog         true      ;# Should we roll log?
ns_param   rollonsignal    true      ;# Roll log on SIGHUP.
ns_param   rollhour        0         ;# Time to roll log.
ns_param   maxbackup       5         ;# Max number to keep around when rolling.
ns_param   logreqtime      true      ;# Log the execution time of request


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
    ns_param nssock ${bindir}/nssock${shlibext}
    #ns_param nslog ${bindir}/nslog${shlibext}
    #ns_param nscgi ${bindir}/nscgi${shlibext}
    #ns_param nsperm ${bindir}/nsperm${shlibext}

#
# nsssl: Only loads if sslcertfile and sslkeyfile exist (see above).
#
#if { [file exists $sslcertfile] && [file exists $sslkeyfile] } {
#    ns_param nsssl ${bindir}/${sslmodule}
#} else {
#    ns_log warning "config.tcl: nsssl not loaded -- key/cert files do not exist."
#}

#
# Example: Host headers based virtual servers.
#
# To enable:
#
# 1. Load comm driver(s) globally.
# 2. Configure drivers as in a virtual server.
# 3. Add a "servers" section to map virtual servers to Host headers.
# 4. Ensure "defaultserver" in comm driver refers to a defined
#    virtual server.
#
#ns_section "ns/modules"
#    ns_param   nssock          ${bindir}/nssock${shlibext}
#
#ns_section "ns/module/nssock"
#    ns_param   port            $httpport
#    ns_param   hostname        $hostname
#    ns_param   address         $address
#    ns_param   defaultserver   server1
#
#ns_section "ns/module/nssock/servers"
#    ns_param   server1         $hostname:$httpport
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

#
# Example:  Web based stats interface.
#
# To enable:
#
# 1. Configure whether or not stats are enabled. (Optional: default = false)
# 2. Configure URL for statistics. (Optional: default = /_stats)
#
#    http://<host>:<port>/_stats
# 
# 3. Configure user. (Optional: default = aolserver)
# 4. Configure password. (Optional: default = stats)
#
# For added security it is recommended that configure your own
# URL, user, and password instead of using the default values.
#
#ns_section ns/server/stats
#    ns_param enabled 1
#    ns_param url /aolserver/stats
#    ns_param user nsadmin
#    ns_param password 23dfs!d
# 
