<%
ns_set update [ns_conn outputheaders] "Expires" "now"

set stat [ns_adp_argv 1]

if [string length $stat] {
    set title "AOLserver Stats: [ns_info hostname] - $stat"
    set nav "<a href=index.adp><font color=#ffffff>Main Menu</font></a> &gt; <font color=#ffcc00>$stat</font>"
} else {
    set title "AOLserver Stats: [ns_info hostname]"
    set nav "<font color=#ffcc00><font color=#ffcc00>Main Menu</font>"
}
%>

<html>
<body bgcolor=#ffffff>
<head>
<title><%=$title%></title>
<style>
    body    { font-family: verdana,arial,helvetica,sans-serif; font-size: 8pt; color: #000000; background-color: #ffffff; }
    td      { font-family: verdana,arial,helvetica,sans-serif; font-size: 8pt; }
    pre     { font-family: courier new, courier; font-size: 10pt; }
    form    { font-family: verdana,helvetica,arial,sans-serif; font-size: 10pt; }
    i       { font-style: italic; }
    b       { font-style: bold; }
    hl      { font-family: verdana,arial,helvetica,sans-serif; font-style: bold; font-size: 12pt; }
    small   { font-size: smaller; }
</style>
</head>

<table border=0 cellpadding=5 cellspacing=0 width="100%">
<tr>
    <td valign=middle bgcolor=#666699><font size=1 color=#ffffff><b><%=$nav%></b></font></td>
    <td valign=middle bgcolor=#666699 align=right><font size=1 color=#ffffff><b><%=[_ns_stats.fmtTime [ns_time]]%></b></font></td>
</tr>
</table>
<br>
