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
# $Header: /Users/dossy/Desktop/cvs/aolserver/tcl/sendmail.tcl,v 1.8 2009/01/19 12:21:08 gneumann Exp $
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
        if {[string range $line 3 3] ne "-"} {
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
    if {$smtp eq ""} {
        set smtp [ns_config ns/parameters mailhost]
    }
    if {$smtp eq ""} {
        set smtp localhost
    }
    set timeout [ns_config ns/parameters smtptimeout]
    if {$timeout eq ""} {
        set timeout 60
    }
    set smtpport [ns_config ns/parameters smtpport]
    if {$smtpport eq ""} {
        set smtpport 25
    }

    set tolist [list]
    foreach toaddr $tolist_in {
        lappend tolist "[string trim $toaddr]"
    }

    set bcclist [list]
    if {$bcclist_in ne ""} {
        foreach bccaddr $bcclist_in {
            lappend bcclist "[string trim $bccaddr]"
        }
    }

    ## Send it along to _ns_sendmail
    _ns_sendmail $smtp $smtpport $timeout $tolist $bcclist \
            $from $subject $body $extraheaders
}


if { ![nsv_exists ns_sendmail sequence] } {
    nsv_set ns_sendmail sequence 0
}

proc _ns_sendmail {smtp smtpport timeout tolist bcclist \
        from subject body extraheaders} {

    ## Put the tolist in the headers
    set rfcto [join $tolist ", "]

    ## Build headers
    set msg "To: $rfcto\nFrom: $from\nSubject: $subject\nDate: [ns_httptime [ns_time]]"

    ## Insert extra headers, if any (not for BCC)
    set message_id_already_done_p 0
    if {$extraheaders ne ""} {
        set size [ns_set size $extraheaders]
        for {set i 0} {$i < $size} {incr i} {
            set key [ns_set key $extraheaders $i]
            if { [string equal $key {Message-ID}] } {
                set message_id_already_done_p 1
            }
            append msg "\n${key}: [ns_set value $extraheaders $i]"
        }
    }

    # Insert a unique "Message-ID:" header, but only if the caller did
    # not manually include a Message-ID header:
    #
    # An application could use the Message-ID header for
    # e.g. threading support, but we're not trying to do anything
    # fancy like that here.  We just want to include a globally-unique
    # ID.  Why?  Well, for one thing, since most email user agents
    # include a Message-ID, but most SPAM software does not, some
    # anti-SPAM software filters out email which does not have a
    # Message-ID...
    #
    # Note: The $message_id below is guaranteed to be globally unique
    # if and only if *ALL* of the following conditions are true:
    #
    # 1. Your unix box's hostname (which is what [ns_info hostname]
    #    returns) is set to a fully-qualified name like
    #    "philip.greenspun.com", NOT just a local hostname like
    #    "philip".
    # 2. Your fully-qualified hostname is in fact globally-unique.
    #    AKA, you didn't do something foolish like set up two separate
    #    machines that both think their hostname is
    #    "philip.greenspun.com".
    # 3. On your unix host, you have only ONE AOLserver running with
    #    the server name returned by [ns_info server].
    # 4. Since [ns_info boottime] is in seconds, you never restart
    #    your AOLserver multiple times in < 1 second, jump your system
    #    clock backwards in time, or etc.
    # 5. Once the "ns_sendmail sequence" nsv variable is set, you
    #    never manually fool with it to re-set it to a previous value.
    #    While the server is running, this value must always increase,
    #    never decrease.
    #
    # --atp@piskorski.com, 2001/10/11 11:51 EDT

    # For more info on messgage-id and other email fields, see RFC 2822:
    #   http://www.faqs.org/rfcs/rfc2822.html

    if { ! $message_id_already_done_p } {
       set message_id "[nsv_incr ns_sendmail sequence].[ns_info boottime].[ns_info server]@[ns_info hostname]"
       append msg "\nMessage-ID: <$message_id>"
    }

    ## Blank line between headers and body
    append msg "\n\n$body\n"

    ## Terminate body with a solitary period
    foreach line [split $msg "\n"] {
        if {"." eq $line} {
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

    ## Strip "from:" email address
    regexp {.*<(.*)>} $from ig from

    ## Perform the SMTP conversation
    if { [catch {
        _ns_smtp_recv $rfp 220 $timeout
        _ns_smtp_send $wfp "HELO [ns_info hostname]" $timeout
        _ns_smtp_recv $rfp 250 $timeout
        _ns_smtp_send $wfp "MAIL FROM:<$from>" $timeout
        _ns_smtp_recv $rfp 250 $timeout

        # TODO: Above, should optionally take a "Return-Path" to use
        # as the envelope sender address (aka, envelope return path)
        # rather than always using $from.  This would allow using
        # VERPs, for instance, as discussed at:
        #   "http://cr.yp.to/proto/verp.txt"
        # See also discussion at:
        #   "http://www.arsdigita.com/bboard/q-and-a-fetch-msg?msg%5fid=000awU"
        # --atp@piskorski.com, 2001/10/11 10:25 EDT

        ## Loop through To and BCC list via multiple RCPT TO lines
        ## A BCC should never, ever appear in the header
        foreach toto [concat $tolist $bcclist] {
             #transform "Fritz <fritz@foo.com>" into "fritz@foo.com"
            regexp {.*<(.*)>} $toto ig toto
            _ns_smtp_send $wfp "RCPT TO:<$toto>" $timeout
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

