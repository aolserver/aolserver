<%
set values [list \
    Host [ns_info hostname] \
    Boottime [clock format [ns_info boottime] -format %c] \
    Label [ns_info label] \
    Version [ns_info version] \
    Build [ns_info builddate] \
    Uptime [_ns_stats.fmtSeconds [ns_info uptime]] \
    KeepAlive [ns_server keepalive] \
    Threads [join [ns_server threads] <br>] \
    Active [join [ns_server active] <br>]]
%>

<table border=0 cellpadding=0 cellspacing=1 bgcolor=#cccccc>
<tr>
    <td valign=middle align=center>
    <table border=0 cellpadding=3 cellspacing=1 width="100%">
    <tr>
        <td valign=middle bgcolor=#999999><font face=verdana size=1 color=#ffffff><nobr>Key</nobr></font></td>
        <td valign=middle bgcolor=#999999><font face=verdana size=1 color=#ffffff><nobr>Value</nobr></font></td>
    </tr>

    <%
    foreach {key value}  $values {
        ns_adp_puts "\
        <tr>
            <td valign=top bgcolor=#ffffff><font face=verdana size=1>$key</font></td>
            <td valign=top bgcolor=#ffffff><font face=verdana size=1>$value</font></td>
        </tr>"
    }
    %>
      
    </table>
    </td>
</tr>
</table>
