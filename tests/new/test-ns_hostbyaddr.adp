<%
source [file dirname [ns_url2file [ns_conn url]]]/harness.tcl

test {
    assertEquals "localhost" [ns_hostbyaddr "127.0.0.1"]

    assertEquals 1 [catch {ns_hostbyaddr "0.0.0.0"} msg]
    assertEquals "could not lookup 0.0.0.0" $msg
}
%>
