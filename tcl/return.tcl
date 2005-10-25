proc ns_returnmoved {url} {
      ns_set update [ns_conn outputheaders] Location $url
      ns_return 301 "text/html" [subst \
{<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML 2.0//EN">
<HTML>
<HEAD>
<TITLE>Moved</TITLE>
</HEAD>
<BODY>
<H2>Moved</H2>
<A HREF="$url">The requested URL has moved here.</A>
<P ALIGN=RIGHT><SMALL><I>[ns_info name]/[ns_info patchlevel] on [ns_conn location]</I></SMALL></P>
</BODY></HTML>}]
}
