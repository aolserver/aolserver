<HTML>

<HEAD>
<TITLE>AOLserver Confidence Test</TITLE>
</HEAD>

<BODY BGCOLOR="#ffffff">

<H2>Test 8</H2>

You should see "blah", what you typed, then nothing for the "notused" variable.

<HR>

thehidden is:
<BR>
<%=[ns_queryget "thehidden"] %>

<P>

What you typed (atext) is:
<BR>
<%=[ns_queryget "atext"] %>

<P>
notused is (should be blank):
<BR>
<%=[ns_queryget "notused"] %>

<P>
(Check your server log for the query set.)
<% ns_set print [ns_conn form $conn] %>

</HTML>
