<%
set col         [ns_queryget col 1]
set reverseSort [ns_queryget reversesort 1]

set numericSort 1
set colTitles   [list Url Conns "Wait s" "Wait ms" "Open s" "Open ms" "Close s" "Close ms" "Avg Wait ms" "Avg Open ms" "Avg Close ms"]

if {$col == 1} {
    set numericSort 0
}

set results ""

foreach url [ns_server urlstats] {
    set name        [lindex $url 0]
    set stats       [lindex $url 1]
    set elems       [split $stats]

    set conns       [lindex $elems 0]
    set sWait       [lindex $elems 1]
    set uWait       [lindex $elems 2]
    set sOpen       [lindex $elems 3]
    set uOpen       [lindex $elems 4]
    set sClose      [lindex $elems 5]
    set uClose      [lindex $elems 6]
    set aWait       [expr ($sWait + .$uWait)/$conns]
    set aOpen       [expr ($sOpen + .$uOpen)/$conns]
    set aClose      [expr ($sClose + .$uClose)/$conns]
    
    lappend results [list $name $conns $sWait $uWait $sOpen $uOpen $sClose $uClose $aWait $aOpen $aClose]
}

set rows [_ns_stats.sortResults $results [expr $col - 1] $numericSort $reverseSort]

if ![string length $rows] {
    ns_adp_puts "<font color=red size=1>Notice: ns_server urlstats not enabled on this server.</font>"
} else {
    ns_adp_include results.inc $col $colTitles url.adp $rows $reverseSort
}
%>
