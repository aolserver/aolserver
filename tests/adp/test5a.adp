<HTML>

<HEAD>
<TITLE>AOLserver Confidence Test</TITLE>
</HEAD>

<BODY BGCOLOR="#ffffff">

<H2>Test 5</H2>

$Header: /Users/dossy/Desktop/cvs/aolserver/tests/adp/test5a.adp,v 1.1 2000/10/09 20:29:54 kriston Exp $

<P>

whatyoutyped:
<BR>
<% ns_puts [ns_queryget "whatyoutyped"] %>

<P>
hiddenfield:
<BR>
<% ns_puts [ns_queryget "hiddenfield"] %>


</BODY>
</HTML>
