<HTML>

<HEAD>
<TITLE>AOLserver Confidence Test</TITLE>
</HEAD>

<BODY BGCOLOR="#ffffff">

<H2>Test 8</H2>

$Header: /Users/dossy/Desktop/cvs/aolserver/tests/adp/test8.adp,v 1.1 2000/10/09 20:29:54 kriston Exp $

<P>

Test submitting forms to an ADP.  Test POST and GET methods.  Ensure
that data at the end of the ACTION URL is discarded in both cases.

<HR>

FORM METHOD=GET ACTION="test8a.adp?notused=blee"

<P>
<FORM METHOD=GET ACTION="test8a.adp?notused=blee">

<INPUT TYPE=hidden NAME="thehidden" VALUE="blah">
<INPUT TYPE=text   NAME="atext">

<INPUT TYPE=submit VALUE="Submit it...">
</FORM>

<HR>

FORM METHOD=POST ACTION="test8a.adp?notused=blee"

<P>
<FORM METHOD=POST ACTION="test8a.adp?notused=blee">

<INPUT TYPE=hidden NAME="thehidden" VALUE="blah">
<INPUT TYPE=text   NAME="atext">

<INPUT TYPE=submit VALUE="Submit it...">
</FORM>

<HR>

POST data to a form that doesn't actually use the data.  Perhaps
useful for scripts with very poor coding style (??).

<P>
<HR>

FORM METHOD=POST ACTION="test8b.adp"

<P>
<FORM METHOD=POST ACTION="test8b.adp">

<INPUT TYPE=hidden NAME="thehidden" VALUE="blah">
<INPUT TYPE=text   NAME="atext">

<INPUT TYPE=submit VALUE="Submit it...">
</FORM>

<HR>

</HTML>
