<%
## Nasty-cheap authenticator.
if {[ns_conn authuser] != "nsadmin" || [ns_conn authpassword] != "x"} {
    ns_returnunauthorized
    ns_adp_break
}

## Expires: now
ns_set update [ns_conn outputheaders] "Expires" "now"
%>
<html>
<head>
<title>AOLserver Telemetry</title>
<STYLE>
    BODY { background: #ffffff; font-family: helvetica; font-size: 10pt; }
    H1   { font-family: helvetica; font-size: 18pt; }
    H2   { font-family: helvetica; font-size: 16pt; }
    H3   { font-family: helvetica; font-size: 14pt; }
    TD   { background: cyan; font-family: helvetica; font-size: 10pt; }
    PRE  { font-family: helvetica; font-size: 10pt; }
    FORM { font-family: helvetica; font-size: 8pt; }
</STYLE>
</head>

<body>

<%
## For Tcl procs: display proc body and immediately return.
if { [ns_queryget "tclproc"] != "" } {
    set tclproc  [ns_queryget tclproc]
    set procbody [info body $tclproc]
    ns_puts "
<h2>Tcl Proc: $tclproc</h2>
<blockquote>
<tt><b>$tclproc [info args $tclproc]</b></tt>
<form target=nothing>
<textarea cols=90 rows=25>$procbody</textarea>
</blockquote>
</form>
</body>
</html>
"
    ns_adp_return
}
%>

<h2>AOLserver Telemetry</h2>

$Header: /Users/dossy/Desktop/cvs/aolserver/tests/nstelemetry.adp,v 1.1 2000/10/09 20:29:54 kriston Exp $

<blockquote>
This is <b>nstelemetry</b> running at <%=[ns_conn url]%> on
<%=[ns_conn server]%> at <%=[ns_httptime [ns_time]]%>.

<br>

For easier reading of these data your browser should support
Style Sheets at least as well as
<a href="http://home.netscape.com/">Netscape 4.x</a> does -- MSIE 5.5
users will see inconsistencies.

</blockquote>

<p>

<h3>Server Information</h3>

<blockquote>

<table>
<%

## Server information.
## Things that don't go here: threads, callbacks, scheduled, sockcallbacks
foreach item {
    server hostname address
    pid uptime boottime
    home config log pageroot tcllib
    nsd argv0 name version label builddate platform
} {
    ## Note the catch so this works on all AOLserver revisions.
    if [catch {
	set itemval [ns_info $item]
	if [regexp {boottime} $item] {
	    set itemval "[ns_httptime $itemval]"
	}
	ns_puts "<tr>
<td align=right valign=top><b>$item:</b></td>
<td align=left valign=top>$itemval &nbsp;</td>
</tr>"
    } errMsg] {
	## Catch commands that don't exist.
	ns_puts "<tr>
<td align=right><b>$item:</b></td><td align=left>n/a</td>
</tr>"
	continue
    }
}
%>

</table>

</blockquote>


<h3>Memory Cache</h3>

<blockquote>

<table>
<tr>
<td align=left valign=top><b>name</b></td>
<td align=left valign=top><b>stats</b></td>
</tr>
<%

foreach item [lsort [ns_cache_names]] {
    ns_puts "<tr><td align=left valign=top>$item</td>"
    ns_puts "<td align=left valign=top>[ns_cache_stats $item]&nbsp;</td></tr>"
}
%>
</table>

</blockquote>

<h3>Thread Locks</h3>

<blockquote>

<table>

<tr>
<td align=left valign=top><b>name</b></td>
<td align=left valign=top><b>owner</b></td>
<td align=left valign=top><b>id</b></td>
<td align=left valign=top><b>nlock</b></td>
<td align=left valign=top><b>nbusy</b></td>
</tr>

<%
foreach item [lsort [ns_info locks]] {
    ns_puts "<tr>"
    ns_puts "<td align=left valign=top>[lindex $item 0]&nbsp;</td>"
    for { set i 1 } { $i < [llength $item] } { incr i } {
	ns_puts "<td align=right valign=top>&nbsp;[lindex $item $i]</td>"
    }
    ns_puts "</tr>"
}
%>

</table>

</blockquote>

<h3>Running Threads</h3>

<blockquote>

<table>

<tr>
<td align=left valign=top><b>name</b></td>
<td align=left valign=top><b>parent</b></td>
<td align=left valign=top><b>tid</b></td>
<td align=left valign=top><b>flags</b></td>
<td align=left valign=top><b>ctime</b></td>
<td align=left valign=top><b>proc</b></td>
<td align=left valign=top><b>arg</b></td>
</tr>


<%
foreach item [lsort [ns_info threads]] {
    ns_puts "<tr>"
    for { set i 0 } { $i < 7 } { incr i } {
	ns_puts "<td align=left valign=top><pre>"
	if { $i != 4 } {
	    ns_puts "[lindex $item $i]"
	} else {
	    ns_puts "[ns_httptime [lindex $item $i]]"
	}
	ns_puts "</pre></td>"
    }
    ns_puts "</tr>"
}
%>

</table>

</blockquote>

<h3>Scheduled Procedures</h3>

<blockquote>

<table>

<tr>
<td align=left valign=bottom><b>proc</b></td>
<td align=left valign=bottom><b>id</b></td>
<td align=left valign=bottom><b>flags</b></td>
<td align=left valign=bottom><b>interval</b></td>
<td align=left valign=bottom><b>nextqueue<br>lastqueue<br>laststart<br>lastend<br></b></td>
<td align=left valign=bottom><b>arg</b></td>
</tr>

<%
foreach item [lsort [ns_info scheduled]] {
    ns_puts "<tr>"

    ## proc, id, flags, interval
    ns_puts "<td align=left valign=top>[lindex $item 7]&nbsp;</td>"

    for { set i 0 } { $i < 3 } { incr i } {
	ns_puts "<td align=left valign=top>[lindex $item $i]&nbsp;</td>"
    }

    ## times: nextqueue, lastqueue, laststart, lastend
    ns_puts "<td align=left valign=top><pre>"
    for { set i 3 } { $i < 7 } { incr i } {
	ns_puts "[ns_httptime [lindex $item $i]]"
    }
    ns_puts "</pre></td>"

    ## arg
    ns_puts "<td align=left valign=top>[lindex $item 8]&nbsp;</td>"

    ns_puts "</tr>"
}
%>

</table>

</blockquote>

<h3>Connections (web clients)</h3>

<blockquote>

<table>

<%
foreach item {
    connections waiting queued keepalive
} {
    ns_puts "<tr><td align=left valign=top><b>$item:</b></td>"
    ns_puts "<td align=right valign=top>&nbsp;[ns_server $item]</td></tr>"
}

%>

</table>

</blockquote>


<h3>Callbacks -- Events</h3>

<blockquote>

<table>

<tr>
<td align=left valign=top><b>event</b></td>
<td align=left valign=top><b>name</b></td>
<td align=left valign=top><b>arg</b></td>
</tr>

<%
foreach item [lsort [ns_info callbacks]] {
    ns_puts "<tr>"
    for { set i 0 } { $i < 3 } { incr i } {
	ns_puts "<td align=left valign=top>[lindex $item $i]&nbsp;</td>"
    }
    ns_puts "</tr>"
}
%>

</table>

</blockquote>

<h3>Callbacks -- Sockets (sockcallbacks)</h3>

<blockquote>

<table>

<tr>
<td align=left valign=top><b>sock id</b></td>
<td align=left valign=top><b>when</b></td>
<td align=left valign=top><b>proc</b></td>
<td align=left valign=top><b>arg</b></td>
</tr>

<%
foreach item [lsort [ns_info sockcallbacks]] {
    ns_puts "<tr>"
    for { set i 0 } { $i < 4 } { incr i } {
	ns_puts "<td align=left valign=top>[lindex $item $i]</td>"
    }
    ns_puts "</tr>"
}
%>

</table>

</blockquote>


<h3>URL Stats</h3>

<blockquote>

<table>

<tr>
<td align=left valign=top><b>url</b></td>
<td align=right valign=top><b>hits</b></td>
<td align=right valign=top><b>wait (sec)</b></td>
<td align=right valign=top><b>open (sec)</b></td>
<td align=right valign=top><b>closed (sec)</b></td>
</tr>

<%
foreach item [lsort [ns_server urlstats]] {
    ns_puts "<tr>"
    ns_puts "<td align=left valign=top>[lindex $item 0]&nbsp;</td>"
    ns_puts "<td align=right valign=top>&nbsp;[lindex [lindex $item 1] 0]</td>"

    for { set i 2 } { $i < 8 } { incr i; incr i} {
	if { [set thistime [lindex [lindex $item 1] $i]] != "" } {
	    ns_puts "
<td align=right valign=top>&nbsp;[format "%.6f" [expr $thistime * .000001]]</td>"
	} else {
	    ns_puts "<td>&nbsp;</td>"
	}
    }
    ns_puts "</tr>"
}
%>
</table>

</blockquote>

<h3>Script and Server Errors</h3>

<blockquote>

Coming soon: Reports of 400- and 500-series errors.

</blockquote>


<h3>Configuration</h3>

<blockquote>

Coming soon: Dump of parsed configuration data.

</blockquote>


<h3>Tcl Information -- Tcl Core</h3>

<blockquote>
<table>

<%
## Tcl library information.
foreach item {
    tclversion patchlevel level
    nameofexecutable sharedlibextension library} {
    ns_puts "<tr><td align=right valign=top><b>$item:</b></td>"
    ns_puts "<td align=left>[info $item]</td></tr>"
}
%>
</table>
</blockquote>


<h3>Tcl Information -- Procs (Tcl language)</h3>

<blockquote>
<font size="-2">
<%
foreach item [lsort [info procs]] {
    ns_puts "<a href=[ns_conn url]?tclproc=[ns_urlencode $item]>$item</a>&nbsp; "
}
%>
</font>
</blockquote>

<h3>Tcl Information -- Commands (C and Tcl language)</h3>

<blockquote>
<font size="-2">
<%
foreach item [lsort [info commands]] {
    ns_puts "${item}&nbsp;"
}
%>
</font>
</blockquote>


<h3>HTTP Headers</h3>

<blockquote>
<pre>
<%
for { set i 0 } { $i < [ns_set size [ns_conn headers]] } { incr i } {
    ns_puts "[ns_set key [ns_conn headers] $i]: [ns_set \
                value [ns_conn headers] $i]"
}
%>
</pre>
</blockquote>



<h3><a name="serverlog">Server Log</a></h3>

<%
if [regexp {STDOUT} [ns_info log]] {
    ns_puts "<b>Log is on stdout and cannot be viewed from here.</b>"
    ns_puts "</body></html>"
    ns_adp_return
}
%>

<%
if { [set logsize [ns_queryget logsize]] == "" } {
    set logsize 50000
}
%>

Last <%=$logsize%> bytes of server log <%=[ns_info log]%>.

<form method=get action=<%=[ns_conn url]%>>
<input type=text name=logsize size=6 maxlength=6>
<input type=submit value="get more">
<p>
</form>

<p>

<form action=nothing>
<textarea cols=90 rows=25><%
if [catch {
    ## Server log can be very small so make sure requested size won't kill us.
    set filesize [file size [ns_info log]]
    set fd [open [ns_info log]]
    if { $filesize > $logsize } {
	seek $fd -$logsize end
    } else {
	seek $fd 0 start
    }
    ns_puts [read $fd]
} errMsg] {
    ns_log error "[ns_conn url]: error accessing log: \"$errMsg\""
    ns_puts "Unable to access server.log: error \"$errMsg\""
}
close $fd
%></textarea>
</form>

<p>
&nbsp;
<p>

</body>
</html>

