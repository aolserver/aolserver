<%
source [file dirname [ns_url2file [ns_conn url]]]/harness.tcl

test {
    assertEquals "" [ns_hrefs {}]
    assertEquals "" [ns_hrefs {<a>}]
    assertEquals "{}" [ns_hrefs {<a href="">}]
    assertEquals "{}" [ns_hrefs {<a href=''>}]

    assertEquals "simple" [ns_hrefs {<a href="simple">}]
    assertEquals "href=tricky" [ns_hrefs {<a href="href=tricky">}]
    assertEquals "first" [ns_hrefs {<a href="first" href="second">}]
    assertEquals "naked" [ns_hrefs {<a href=naked dummy>}]

    assertEquals "" [ns_hrefs {a href="bogus">}]
    assertEquals "" [ns_hrefs {<a href="bogus"}]
    assertEquals "" [ns_hrefs {<a href "bogus">}]
    assertEquals "" [ns_hrefs {<a "href="bogus"">}]

    # [ 995078 ] ns_hrefs only checks first attribute in <a>
    assertEquals "hard" [ns_hrefs {<a dummy href="hard">}]
}
%>
