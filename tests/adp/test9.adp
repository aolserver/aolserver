<HTML>

<HEAD>
<TITLE>AOLserver Confidence Test</TITLE>
</HEAD>

<BODY BGCOLOR="#ffffff">

<H2>Test 9</H2>

$Header: /Users/dossy/Desktop/cvs/aolserver/tests/adp/test9.adp,v 1.1 2000/10/09 20:29:54 kriston Exp $

<P>

A call to ns_adp_include, while in the same interpreter and thread,
still follows the rules of calling a subproc.  This means that the
variables in the ns_adp_include are not available unless you "global"
or "upvar" them.  Likewise, variables of the same names in the subproc
are unaffected by the calling proc.

<p>

<b>Results should be:</b>

<blockquote>
<small>
spoo, 123, blahblahblah
<br>
spoo, 123, blahblahblah 
<br>
unchanged1, unchanged2, unchanged3 
<br>
spoo, 123, blah 
<br>
unchanged1, unchanged2, unchanged3 
<br>
spoo, 123, blah 
</small>
</blockquote>

<h3>Test begins:</h3>

<%
set one "spoo"
set two 123
set three "blahblahblah"
%>

<b>
<%=$one%>, <%=$two%>, <%=$three%>
</b>

<hr>

<% ns_adp_include "test9.inc" %>

<hr>

<b>
<%=$one%>, <%=$two%>, <%=$three%>
</b>

<p>
&nbsp;
<p>

</BODY>
</HTML>
