<%
set col         [ns_queryget col 1]
set reverseSort [ns_queryget reversesort 1]

set numericSort     1
set colTitles       [list Command "Times Invoked"]
set rows            ""

if {$col == 1} {
    set numericSort 0
}
 
set rows [_ns_stats.sortResults [ns_stats] [expr $col - 1] $numericSort $reverseSort]

if ![string length $rows] {
    ns_adp_puts "<font color=red size=1><b>Notice: ns_stats is not enabled on this server.</font>"
} else {
    ns_adp_include results.inc $col $colTitles commands.adp $rows $reverseSort
}
%>
