<HTML>

<HEAD>
<TITLE>AOLserver Confidence Test</TITLE>
</HEAD>

<BODY BGCOLOR="#ffffff">

<H2>Test 2</H2>

$Header: /Users/dossy/Desktop/cvs/aolserver/tests/adp/test2.adp,v 1.1 2000/10/09 20:29:54 kriston Exp $

<P>

Complex Tcl commands will be run:

<HR>

Do a foreach with ns_set:
<BR>

<%

set myset [ns_set create "My Set"]

foreach thing { one two three four } {
    ns_set put $myset item${thing} "Thing number $thing."
}
ns_puts "the set is [ns_set size $myset] elements large"

for { set i 0 } { $i < [ns_set size $myset] } { incr i } {
    ns_puts "<BR>[ns_set value $myset $i]"
}

%>

<HR>


</BODY>
</HTML>
