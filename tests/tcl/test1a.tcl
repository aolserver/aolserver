ns_return 200 text/html "
<HTML>

<HEAD>
<TITLE>AOLserver Confidence Test</TITLE>
</HEAD>

<BODY BGCOLOR=\"#ffffff\">

the hidden is:
<BR>
[ns_queryget "thehidden"]

<P>

What you typed (atext) is:
<BR>
[ns_queryget "atext"]

<P>
notused is (should be blank):
<BR>
[ns_queryget "notused"]

<P>
(Check your server log for the query set.)
[ns_set print [ns_conn form $conn ]]

</BODY>
</HTML>
"
