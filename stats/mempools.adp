<%
set talloc 0
set trequest 0
set tused 0
set tlocks 0
set twaits 0
set tfree 0
set tops 0   

ns_adp_include inc/header.inc mempools

ns_adp_puts "\
<table border=0 cellpadding=0 cellspacing=0>
<tr>
    <td valign=middle>"
    
foreach p [lsort [ns_info pools]] {
    ns_adp_puts "\
    <b>[lindex $p 0]:</b>
    <br><br>
    <table border=0 cellpadding=0 cellspacing=1 bgcolor=#cccccc width=\"100%\">
    <tr>
        <td valign=middle align=center>
        <table border=0 cellpadding=4 cellspacing=1 width=\"100%\">
        <tr>
            <td valign=middle bgcolor=#999999><font color=#ffffff>Block Size</font></td>
            <td valign=middle bgcolor=#999999><font color=#ffffff>Frees</font></td>
            <td valign=middle bgcolor=#999999><font color=#ffffff>Gets</font></td>
            <td valign=middle bgcolor=#999999><font color=#ffffff>Puts</font></td>
            <td valign=middle bgcolor=#999999><font color=#ffffff>Bytes Req</font></td>
            <td valign=middle bgcolor=#999999><font color=#ffffff>Bytes Used</font></td>
            <td valign=middle bgcolor=#999999><font color=#ffffff>Overhead</font></td>
            <td valign=middle bgcolor=#999999><font color=#ffffff>Locks</font></td>
            <td valign=middle bgcolor=#999999><font color=#ffffff>Lock Waits</font></td>
         </tr>"
   
	foreach b [lrange $p 1 end] {
		set bs [lindex $b 0]
		set nf [lindex $b 1]
		set ng [lindex $b 2]
		set np [lindex $b 3]
		set nr [lindex $b 4]
		set nu [expr $ng - $np]
		set na [expr $nu * $bs]
		
		incr tops [expr $ng + $np]
		incr tlocks [lindex $b 5]
		incr twaits [lindex $b 6]
		incr tfree [expr $bs * $nf]
		incr talloc $na
		incr trequest $nr
		incr tused $nu
		
		if {$nr != 0} {
			set ov [expr $na - $nr]
			set op [format %4.2f%% [expr $ov.0 * 100 / $nr.0]]
		} else {
			set ov "N/A"
			set op "N/A"
		}
		
		ns_adp_puts "<tr>"
		
		foreach e [linsert [lreplace $b 4 4] 4 $nr $na $op] {
			ns_adp_puts "<td bgcolor=#ffffff>$e</td>"
		}
		
		ns_adp_puts "</tr>"
	}
	
	ns_adp_puts "\
	</table>
	</td>
</tr>
</table>
<br>"
}

set ov [expr $talloc - $trequest]
set op [format %4.2f [expr $ov.0 * 100 / $trequest.0]]
set av [format %4.2f [expr 100.0 - ($tlocks.0 * 100) / $tops.0]]

if {$tlocks > 0} {
	set wr [format %4.2f [expr $twaits.0 / $tlocks.0]]
} else {
	set wr N/A
}

ns_adp_puts "\
    </td>
</tr>
<tr>
    <td valign=middle>
    <b>Totals:</b><br><br>
    <table>
        <tr><td>Bytes Requested:</td><td>$trequest</td></tr>
        <tr><td>Bytes Free:</td><td>$tfree</td></tr>
        <tr><td>Bytes Allocated:</td><td>$talloc</td></tr>
        <tr><td>Bytes Wasted:</td><td>$ov</td></tr>
        <tr><td>Byte Overhead:</td><td>${op}%</td></tr>
        <tr><td>Mutex Locks:</td><td>$tlocks</td></tr>
        <tr><td>Mutex Lock Waits:</td><td>$twaits</td></tr>
        <tr><td>Lock Wait Ratio:</td><td>${wr}%</td></tr>
        <tr><td>Gets/Puts:</td><td>${tops}</td></tr>
        <tr><td>Lock Avoidance:</td><td>${av}%</td></tr>
    </table>
    </td>
</tr>
</table>"

ns_adp_include inc/footer.inc
%>
