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
# $Header: /Users/dossy/Desktop/cvs/aolserver/tcl/sendmail.tcl,v 1.4 2002/02/08 07:56:16 hobbs Exp $
#

#
# sendmail.tcl - Define the ns_sendmail procedure for sending
# email from a Tcl script through a remote SMTP server.
#


proc _ns_smtp_send {wfp string timeout} {
    if {[lindex [ns_sockselect -timeout $timeout {} $wfp {}] 1] == ""} {
	error "Timeout writing to SMTP host"
    }
    puts $wfp $string\r
    flush $wfp
}


proc _ns_smtp_recv {rfp check timeout} {
    while {1} {
	if {[lindex [ns_sockselect -timeout $timeout $rfp {} {}] 0] == ""} {
	    error "Timeout reading from SMTP host"
	}
	set line [gets $rfp]
	set code [string range $line 0 2]
	if {![string match $check $code]} {
	    error "Expected a $check status line; got:\n$line"
	}
	if {![string match "-" [string range $line 3 3]]} {
	    break;
	}
    }
}


proc ns_sendmail { to from subject body {extraheaders {}} {bcc {}} } {

    ## Takes comma-separated values in the "to" parm
    ## Multiple To and BCC addresses are handled appropriately.
    ## Original ns_sendmail functionality is preserved.

    ## Cut out carriage returns
    regsub -all "\n" $to "" to
    regsub -all "\r" $to "" to
    regsub -all "\n" $bcc "" bcc
    regsub -all "\r" $bcc "" bcc
    
    ## Split to into a proper list
    set tolist_in [split $to ","]
    set bcclist_in [split $bcc ","]
    
    ## Get smtp server into, if none then use localhost
    set smtp [ns_config ns/parameters smtphost]
    if {[string match "" $smtp]} {
	set smtp [ns_config ns/parameters mailhost]
    }
    if {[string match "" $smtp]} {
	set smtp localhost
    }
    set timeout [ns_config ns/parameters smtptimeout]
    if {[string match "" $timeout]} {
	set timeout 60
    }
    set smtpport [ns_config ns/parameters smtpport]
    if {[string match "" $smtpport]} {
	set smtpport 25
    }

    ## Extract "from" email address
    if {[regexp {.*<(.*)>} $from ig address]} {
	set from $address
    }
    
    set tolist [list]
    foreach toaddr $tolist_in {
	if {[regexp {.*<(.*)>} $toaddr ig address]} {
	    set toaddr $address
	}
	lappend tolist "[string trim $toaddr]"
    }
    
    set bcclist [list]
    if {![string match "" $bcclist_in]} {
	foreach bccaddr $bcclist_in {
	    if {[regexp {.*<(.*)>} $bccaddr ig address]} {
		set bccaddr $address
	    }
	    lappend bcclist "[string trim $bccaddr]"
	}
    }
    
    ## Send it along to _ns_sendmail
    _ns_sendmail $smtp $smtpport $timeout $tolist $bcclist \
	    $from $subject $body $extraheaders
}


proc _ns_sendmail {smtp smtpport timeout tolist bcclist \
	from subject body extraheaders} {
    
    ## Put the tolist in the headers
    set rfcto [join $tolist ", "]
    
    ## Build headers
    set msg "To: $rfcto\nFrom: $from\nSubject: $subject\nDate: [ns_httptime [ns_time]]"
    
    ## Insert extra headers, if any (not for BCC)
    if {![string match "" $extraheaders]} {
	set size [ns_set size $extraheaders]
	for {set i 0} {$i < $size} {incr i} {
	    append msg "\n[ns_set key $extraheaders $i]: [ns_set value $extraheaders $i]"
	}
    }
    
    ## Blank line between headers and body
    append msg "\n\n$body\n"
    
    ## Terminate body with a solitary period
    foreach line [split $msg "\n"] { 
	if {[string match . $line]} {
	    append data .
	}
	append data $line
	append data "\r\n"
    }
    append data .
    
    ## Open the connection
    set sock [ns_sockopen $smtp $smtpport]
    set rfp [lindex $sock 0]
    set wfp [lindex $sock 1]

    ## Perform the SMTP conversation
    if { [catch {
	_ns_smtp_recv $rfp 220 $timeout
	_ns_smtp_send $wfp "HELO AOLserver [ns_info hostname]" $timeout
	_ns_smtp_recv $rfp 250 $timeout
	_ns_smtp_send $wfp "MAIL FROM:<$from>" $timeout
	_ns_smtp_recv $rfp 250 $timeout
	
	## Loop through To list via multiple RCPT TO lines
	foreach toto $tolist {
	    _ns_smtp_send $wfp "RCPT TO:<$toto>" $timeout
	    _ns_smtp_recv $rfp 250 $timeout	
	}
	
	## Loop through BCC list via multiple RCPT TO lines
	## A BCC should never, ever appear in the header.  Ever.  Not even.
	foreach bccto $bcclist {
	    _ns_smtp_send $wfp "RCPT TO:<$bccto>" $timeout
	    _ns_smtp_recv $rfp 250 $timeout
	}
	
	_ns_smtp_send $wfp DATA $timeout
	_ns_smtp_recv $rfp 354 $timeout
	_ns_smtp_send $wfp $data $timeout
	_ns_smtp_recv $rfp 250 $timeout
	_ns_smtp_send $wfp QUIT $timeout
	_ns_smtp_recv $rfp 221 $timeout
    } errMsg ] } {
	## Error, close and report
	close $rfp
	close $wfp
	return -code error $errMsg
    }

    ## Close the connection
    close $rfp
    close $wfp
}

