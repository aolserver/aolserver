<HTML>

<HEAD>
<TITLE>AOLserver Confidence Test</TITLE>
</HEAD>

<BODY BGCOLOR="#ffffff">

<H2>Test 1</H2>

$Header: /Users/dossy/Desktop/cvs/aolserver/tests/cdev/test1.adp,v 1.1 2000/10/09 20:29:54 kriston Exp $

<P>

Run a custom Tcl command "ns_hello" from nsexample.so:

<P>

<%

if { [info commands ns_hello] == "" } {

    ns_puts "Sorry, you needed to load nsexample.so into your server."

} else {

    ns_puts [ns_hello "Confident Tester Person"]

}
%>


</BODY>
</HTML>
