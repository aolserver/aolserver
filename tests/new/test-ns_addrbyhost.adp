<%
source [file dirname [ns_url2file [ns_conn url]]]/harness.tcl

test {
    assertEquals "127.0.0.1" [ns_addrbyhost "localhost"]

    assertEquals 1 [catch {ns_addrbyhost "this_should_not_resolve"} msg]
    assertEquals "could not lookup this_should_not_resolve" $msg
}
%>
