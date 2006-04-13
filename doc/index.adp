<html>
<head>
<title>AOLserver Documentation</title>
</head>
<body>
<%
set dir [file dirname [ns_url2file [ns_conn url]]]
foreach {title section} [list "Programs" 1 "Tcl Commands" n "Library Routines" 3] {
	ns_adp_puts "<h1>$title</h1>"
	ns_adp_puts <table>
	set n 0
	set ncols 3
	foreach file [lsort [glob -nocomplain $dir/*.$section.htm]] {
		if !$n {
			ns_adp_puts <tr>
		}
		set link [file tail $file]
		set page [file rootname [file rootname $link]]
		ns_adp_puts "<td><a href=$link>$page</a><br></td>"
		if {[incr n] == $ncols} {
			ns_adp_puts </tr>
			set n 0
		}
	}
	ns_adp_puts </table>
}
%>
</body>
</html>
