ns_return 200 text/html "
<HTML>

<HEAD>
<TITLE>AOLserver Confidence Test</TITLE>
</HEAD>

<BODY BGCOLOR=\"#ffffff\">

<H2>Test 1</H2>

<P>

Test submitting forms to a Tcl page.  Tcl pages are nothing more than
Tcl scripts that are sourced in-place in your pageroot.  They are far
inferior to ADP's but some really antiquated applications still use
them.  You need to enable Tcl pages in your nsd.tcl for these tests to
work at all -- they are off by default since they represent a
significant security risk.

<HR>

FORM METHOD=GET ACTION=\"test1a.tcl\"

<P>
<FORM METHOD=GET ACTION=\"test1a.tcl\">

<INPUT TYPE=hidden NAME=\"thehidden\" VALUE=\"blah\">
<INPUT TYPE=text   NAME=\"atext\">

<INPUT TYPE=submit VALUE=\"Submit it...\">
</FORM>

<HR>

FORM METHOD=POST ACTION=\"test1a.tcl\"

<P>
<FORM METHOD=POST ACTION=\"test1a.tcl\">

<INPUT TYPE=hidden NAME=\"thehidden\" VALUE=\"blah\">
<INPUT TYPE=text   NAME=\"atext\">

<INPUT TYPE=submit VALUE=\"Submit it...\">
</FORM>


<HR>

This page does nothing with the data and merely returns an html page.
Only the most sloppy scripts do this in real life, though.

<BR>
FORM METHOD=GET ACTION=\"test1b.tcl\"

<P>
<FORM METHOD=POST ACTION=\"test1b.tcl\">

<INPUT TYPE=hidden NAME=\"thehidden\" VALUE=\"blah\">
<INPUT TYPE=text   NAME=\"atext\">

<INPUT TYPE=submit VALUE=\"Submit it...\">
</FORM>


</BODY>
</HTML>
"
