<HTML>

<HEAD>
<TITLE>AOLserver Confidence Test</TITLE>
</HEAD>

<BODY BGCOLOR="#ffffff">


<H2>AOLserver Confidence Tests</H2>

$Header: /Users/dossy/Desktop/cvs/aolserver/tests/index.adp,v 1.1 2000/10/09 20:29:54 kriston Exp $

<P>

Smoke-test your server with these ADP scripts.  More and more tests
will be added soon.


<H3>Core AOLserver and ADP Functionality</H3>

<BLOCKQUOTE>

<A HREF="adp/test1.adp">Regular Tcl commands</A>

<BR>

<A HREF="adp/test2.adp">Complex Tcl commands</A>

<BR>

<A HREF="adp/test3.adp">ns_adp_include</A>

<BR>

<A HREF="adp/test4.adp">ns_adp_include with args</A>

<BR>

<A HREF="adp/test5.adp">POST data to an ADP</A>

<BR>

<A HREF="adp/test6.adp">Do a Tcl exec on something</A>

<BR>

<A HREF="adp/test7.adp">Test ns_adp_include with ns_adp_return</A>

<BR>

<A HREF="adp/test8.adp">Test GET and POST method forms</A>

<BR>

<A HREF="adp/test9.adp">ADP variable scope with ns_adp_include</A>


</BLOCKQUOTE>

<H3>Selected C API Functions</H3>

<BLOCKQUOTE>

<A HREF="cdev/test1.adp">Run a custom Tcl command (needs nsexample.so)</A>

<BR>

</BLOCKQUOTE>

<H3>Database</H3>

<BLOCKQUOTE>

<A HREF="dbase/test1.adp"></A>Select queries

<BR>

<A HREF="dbase/test2.adp"></A>Insert queries

<BR>

<A HREF="dbase/test3.adp"></A>DDL queries

</BLOCKQUOTE>


<H3>Security</H3>

<BLOCKQUOTE>

<A HREF="sec/test1.adp"></A>User access control

<BR>

<A HREF="sec/test2.adp"></A>Host access control

<BR>

<A HREF="sec/test3.adp"></A>Safe-Tcl interpreter

</BLOCKQUOTE>

<H3>Tcl pages in the pageroot (*.tcl)</H3>

You must have Tcl pages enabled to try these tests.  Tcl pages are
only used by the more antiquated applications out there and represent
a significant security risk.  If your application is still using Tcl
pages, it's time to convert to ADP.

<BLOCKQUOTE>

<A HREF="tcl/test1.tcl">Test POST and GET method forms</A>

</BLOCKQUOTE>

</BODY>
</HTML>
