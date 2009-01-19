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
# $Header: /Users/dossy/Desktop/cvs/aolserver/tcl/http.tcl,v 1.15 2009/01/19 12:21:08 gneumann Exp $
#

# http.tcl -
#	Routines for opening non-blocking HTTP connections through
#	the Tcl socket interface.
#

#
# ==========================================================================
# API procs
# ==========================================================================

#
# ns_httpopen -
#	Fetch a web page; see docs for full details.
#
# Results:
#	A tcl list {read file handle} {write file handle} {result headers set}
#
# Side effects:
#	May throw an error on failure.
#

proc ns_httpopen {method url {rqset ""} {timeout 30} {pdata ""}} {
    #
    # Determine if url is local; prepend site address if so
    #

    if {[string match /* $url]} {
        set host "http://[ns_config ns/server/[ns_info server]/module/nssock hostname]"
        set port [ns_config ns/server/[ns_info server]/module/nssock port]             
        if { $port != 80 } {
            append host ":$port"
        }
        set url "$host$url"
    }

    #
    # Verify that the URL is an HTTP url.
    #
    
    if {![string match "http://*" $url]} {
	return -code error "Invalid url \"$url\": "\
		"ns_httpopen only supports HTTP"
    }

    #
    # Find each element in the URL
    #
    
    set url [split $url /]
    set hp [split [lindex $url 2] :]
    set host [lindex $hp 0]
    set port [lindex $hp 1]
    if {$port eq ""} {
	set port 80
    }
    set uri /[join [lrange $url 3 end] /]

    #
    # Open a TCP connection to the host:port
    #
    
    set fds [ns_sockopen -nonblock $host $port]
    set rfd [lindex $fds 0]
    set wfd [lindex $fds 1]
    if {[catch {
	#
	# First write the request, then the headers if they exist.
	#
	
	_ns_http_puts $timeout $wfd "$method $uri HTTP/1.0\r"
	
	if {$rqset ne ""} {
	    #
	    # There are request headers
	    #
	    
	    for {set i 0} {$i < [ns_set size $rqset]} {incr i} {
		set key [ns_set key $rqset $i]
		set val [ns_set value $rqset $i]
		_ns_http_puts $timeout $wfd "$key: $val\r"
	    }
	} else {
	    #
	    # No headers were specified, so send a minimum set of
	    # required headers.
	    #
	    
	    _ns_http_puts $timeout $wfd "Accept: */*\r"
	    _ns_http_puts $timeout $wfd \
		    "User-Agent: [ns_info name]-Tcl/[ns_info version]\r"
	}

	#
	# Always send a Host: header because virtual hosting happens
	# even with HTTP/1.0.
	#
	
	if { $port == 80 } {
	    set hostheader "Host: ${host}\r"
	} else {
	    set hostheader "Host: ${host}:${port}\r"
	}
	_ns_http_puts $timeout $wfd $hostheader

	#
	# If optional content exists, then output that. Otherwise spit
	# out a newline to end the headers.
	#
	
	if {$pdata ne ""} {
	    _ns_http_puts $timeout $wfd "\r\n$pdata\r"
	} else {
	    _ns_http_puts $timeout $wfd "\r"
	}
	flush $wfd

	#
	# Create a new set; its name will be the result line from
	# the server. Then read headers into the set.
	#
	
	set rpset [ns_set new [_ns_http_gets $timeout $rfd]]
	while {1} {
	    set line [_ns_http_gets $timeout $rfd]
	    if {![string length $line]} {
		break
	    }
	    ns_parseheader $rpset $line
	}
    } errMsg]} {
	#
	# Something went wrong during the request, so return an error.
	#
	
	global errorInfo
	close $wfd
	close $rfd
	if {[info exists rpset]} {
	    ns_set free $rpset
	}
	return -code error -errorinfo $errorInfo $errMsg
    }

    #
    # Return a list of read file, write file, and headers set.
    #
    
    return [list $rfd $wfd $rpset]
}

#
# ns_httppost -
#	Perform a POST request. This wraps ns_httpopen.
#
# Results:
#	The URL content.
#
# Side effects:
#

proc ns_httppost {url {rqset ""} {qsset ""} {type ""} {timeout 30}} {
    #
    # Build the request. Since we're posting, we have to set
    # content-type and content-length ourselves. We'll add these to
    # rqset, overwriting if they already existed, which they
    # shouldn't.
    #

    if {$rqset eq ""} { 
	set rqset [ns_set new rqset]
	ns_set put $rqset "Accept" "*/*\r"
	ns_set put $rqset "User-Agent" "[ns_info name]-Tcl/[ns_info version]\r"
    }
    if {$type eq ""} {
	ns_set put $rqset "Content-type" "application/x-www-form-urlencoded"
    } else {
	ns_set put $rqset "Content-type" "$type"
    }

    #
    # Build the query string to POST with
    #

    set querystring ""
    if {$qsset ne ""} {
	for {set i 0} {$i < [ns_set size $qsset]} {incr i} {
	    set key [ns_set key $qsset $i]
	    set value [ns_set value $qsset $i]
	    if { $i > 0 } {
		append querystring "&"
	    }
	    append querystring "$key=[ns_urlencode $value]"
	}
	ns_log debug "QS that will be sent is $querystring"
	ns_set put $rqset "Content-length" [string length $querystring]
    } else {
        ns_log debug	"QS string is empty"
	ns_set put $rqset "Content-length" "0"
    }

    #
    # Perform the actual request.
    #
    
    set http [ns_httpopen POST $url $rqset $timeout $querystring]
    set rfd [lindex $http 0]
    close [lindex $http 1]
    set headers [lindex $http 2]

    set length [ns_set iget $headers content-length]
    if {$length eq ""} {
	set length -1
    }
    set err [catch {
	#
	# Read the content.
	#
	
	while {1} {
	    set buf [_ns_http_read $timeout $rfd $length]
	    append page $buf
	    if {$buf eq ""} {
		break
	    }
	    if {$length > 0} {
		incr length -[string length $buf]
		if {$length <= 0} {
		    break
		}
	    }
	}
    } errMsg]

    ns_set free $headers
    close $rfd
    if {$err} {
	global errorInfo
	return -code error -errorinfo $errorInfo $errMsg
    }
    return $page
}

#
# ns_httpget -
#	Perform a GET request. This wraps ns_httpopen, but it also
#	knows how to follow redirects and will read the content into
#	a buffer.
#
# Results:
#	The URL content.
#
# Side effects:
#	Will only follow redirections 10 levels deep.
#

proc ns_httpget {url {timeout 30} {depth 0} {rqset ""}} {
    if {[incr depth] > 10} {
	return -code error "ns_httpget: Recursive redirection: $url"
    }
    
    #
    # Perform the actual request.
    #
    
    set http [ns_httpopen GET $url $rqset $timeout]
    set rfd [lindex $http 0]
    close [lindex $http 1]
    set headers [lindex $http 2]
    set response [ns_set name $headers]
    set status [lindex $response 1]
    if {$status == 302} {
	#
	# The response was a redirect, so free the headers and
	# recurse.
	#
	set location [ns_set iget $headers location]
	if {$location ne ""} {
	    ns_set free $headers
	    close $rfd
	    if {[string first http:// $location] != 0} {
		set url2 [split $url /]
		set hp [split [lindex $url2 2] :]
		set host [lindex $hp 0]
		set port [lindex $hp 1]
		if {$port eq ""} {
                    set port 80
                }
		regexp "^(.*)://" $url match method
		
		set location "$method://$host:$port/$location"
	    }
	    return [ns_httpget $location $timeout $depth]
	}
    }
    
    set length [ns_set iget $headers content-length]
    if {$length eq ""} {
	set length -1
    }
    set err [catch {
	#
	# Read the content.
	#
	
	while {1} {
	    set buf [_ns_http_read $timeout $rfd $length]
	    append page $buf
	    if {$buf eq ""} {
		break
	    }
	    if {$length > 0} {
		incr length -[string length $buf]
		if {$length <= 0} {
		    break
		}
	    }
	}
    } errMsg]

    ns_set free $headers
    close $rfd
    if {$err} {
	global errorInfo
	return -code error -errorinfo $errorInfo $errMsg
    }
    return $page
}


# ==========================================================================
# Local procs
# ==========================================================================

#
# _ns_http_readable -
#	Return the number of bytes available to read from a
# 	socket without blocking, waiting up to $timeout seconds for bytes to
# 	arrive if none are currently available.
#
# Results:
#	Number of bytes that are waiting to be read
#

proc _ns_http_readable {timeout sock} {
    set nread [ns_socknread $sock]
    if {!$nread} {
	set sel [ns_sockselect -timeout $timeout $sock {} {}]
	if {"" eq [lindex $sel 0]} {
	    return -code error "ns_sockreadwait: Timeout waiting for remote"
	}
	set nread [ns_socknread $sock]
    }
    return $nread
}


#
# _ns_http_read -
#	Read upto $length bytes from a socket without blocking.
#
# Results:
#	Up to $length bytes that were read from the socket. May
#	throw and error on EOF.
#

proc _ns_http_read {timeout sock length} {
    set buf ""
    set nread [_ns_http_readable $timeout $sock]
    if {$nread > 0} {
	if {$length > 0 && $length < $nread} {
	    set nread $length
	}
	set buf [read $sock $nread]
    }
    return $buf
}


#
# _ns_http_gets -
#	Carefully read lines, one character at a time, from a
# 	socket to avoid blocking.
#
# Results:
#	One line read from the socket
#

proc _ns_http_gets {timeout sock} {
    set line ""
    set done 0
    while {!$done} {
	set nline [_ns_http_readable $timeout $sock]
	if {!$nline} {set done 1}
	while {!$done && $nline > 0} {
	    set char [read $sock 1]
	    if {$char eq "\n"} {set done 1}
	    append line $char
	    incr nline -1
	}
    }
    string trimright $line
}


#
# _ns_http_puts -
#	Send a string out a socket.  If the socket buffer is
# 	full, wait for up to $timeout seconds.
#
# Results:
#	None.
#
# Side effects:
#	May return with an error code on timeout.
#

proc _ns_http_puts {timeout sock string} {
    if {[lindex [ns_sockselect -timeout $timeout {} $sock {}] 1] == ""} {
	return -code error "ns_socksend: Timeout writing to socket"
    }
    puts $sock $string
}


