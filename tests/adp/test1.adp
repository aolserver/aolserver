<HTML>

<HEAD>

<TITLE>AOLserver Confidence Test</TITLE>

</HEAD>

<BODY BGCOLOR="#ffffff">

<H2>Test 1</H2>

$Header: /Users/dossy/Desktop/cvs/aolserver/tests/adp/test1.adp,v 1.1 2000/10/09 20:29:54 kriston Exp $

<P>

Easy Tcl commands will be run:

<HR>

ns_puts:
<BR>

<%

ns_puts "This is a test.<BR>"

%>

<P>

foreach:
<BR>

<%
foreach thing { one two three four } {
    ns_puts "Thing number $thing.<BR>"
}
%>

<HR>


</BODY>
</HTML>
