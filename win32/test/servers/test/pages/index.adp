<a href=/cgitest.bat>Batch CGI test</a><br>
<a href=/cgi/cgitest.exe>EXE CGI test</a><br>
<a href=/cgi/nph-cgitest.exe>NPH CGI test</a><br>
<%
set q ""
set g ""
set f [ns_conn form]
if {$f != ""} {
	set q [ns_set get $f query]
	set g [ns_set get $f go]
}
set infos [list threads locks scheduled callbacks sockcallbacks]
%>
<form>
<textarea name=query cols=80 rows=8><%=$q%></textarea>
<br>
<%
foreach b [concat eval join sort $infos] {
	ns_puts "<input type=submit name=go value=$b>"
}
%>
</form>
<pre>
<%
set result ""
if {[lsearch $infos $g] >= 0} {
	set result [join [lsort [ns_info $g]] \n]
} elseif {$q != ""} {
	set err [catch {set result [eval $q]} errMsg]
	if $err {
		ns_puts "<b>Tcl Error:</b><br>"
		ns_puts [ns_quotehtml $errMsg]
	} else {
		ns_puts "<b>Tcl Output:</b><br>"
		switch $g {
			join {
				set result [join $result \n]
			}
			sort {
				set result [join [lsort $result] \n]
			}
		}
	}
}
ns_puts [ns_quotehtml $result]
%>
</pre>
<form action=/cgi/cgitest.exe method=post><textarea name=foo>Stuff for CGI</textarea><input type=submit></form>