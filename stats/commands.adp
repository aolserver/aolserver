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

ns_adp_include inc/header.inc commands

if ![string length $rows] {
    ns_adp_include inc/msg.inc warning "ns_stats not enabled on this server"
} else {
    ns_adp_include inc/results.inc $col $colTitles commands.adp $rows $reverseSort
}

ns_adp_include inc/footer.inc
%>
