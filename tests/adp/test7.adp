<HTML>

<HEAD>
<TITLE>AOLserver Confidence Test</TITLE>
</HEAD>

<BODY BGCOLOR="#ffffff">

<H2>Test 7</H2>

$Header: /Users/dossy/Desktop/cvs/aolserver/tests/adp/test7.adp,v 1.1 2000/10/09 20:29:54 kriston Exp $

<P>

ns_adp_include with ns_adp_return.  Both nsd76 and nsd8x should see the following:

<SMALL><SMALL>
<BR>
Top of test7.adp.
<BR>
Top of test7b.adp.
<BR>
Top of test7c.adp.
<BR>
Bottom of test7b.adp.
<BR>
Bottom of test7.adp.
</SMALL></SMALL>

<P>

<HR>

Top of test7.adp.
<P>

<% ns_adp_include "test7b.adp" %>

<P>
Bottom of test7.adp.

</BODY>
</HTML>
