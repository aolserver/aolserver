<%
set col         [ns_queryget col 1]
set reverseSort [ns_queryget reversesort 1]

set numericSort 1
set colTitles   [list Thread Parent ID Flags "Create Time" Proc Args]
set rows        ""

if {$col == 1 || $col == 2 || $col == 6 || $col == 7} {
    set numericSort 0
}

set rows ""

foreach t [_ns_stats.sortResults [ns_info threads] [expr $col - 1] $numericSort $reverseSort] {
    set thread  [lindex $t 0]
    set parent  [lindex $t 1]
    set id      [lindex $t 2]
    set flags   [_ns_stats.getThreadType [lindex $t 3]]
    set create  [_ns_stats.fmtTime [lindex $t 4]]
    set proc    [lindex $t 5]
    set arg     [lindex $t 6]
    
    lappend rows [list $thread $parent $id $flags $create $proc $arg]
}

ns_adp_include inc/header.inc threads
ns_adp_include inc/results.inc $col $colTitles threads.adp $rows $reverseSort
ns_adp_include inc/footer.inc
%>
