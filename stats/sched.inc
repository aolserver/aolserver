<%
set col             [ns_queryget col 1]
set reverseSort     [ns_queryget reversesort 1]

set numericSort     1
set scheduledProcs  ""

foreach s [ns_info scheduled] {
    set id          [lindex $s 0]
    set flags       [lindex $s 1]
    set next        [lindex $s 3]
    set lastqueue   [lindex $s 4]
    set laststart   [lindex $s 5]
    set lastend     [lindex $s 6]
    set proc        [lindex $s 7]
    set arg         [lindex $s 8]

    if [catch {
        set duration [expr $lastend - $laststart]
    }] {
        set duration "0"
    }

    set state "pending"

    if [_ns_stats.isThreadSuspended $flags] {
        set state suspended
    }

    if [_ns_stats.isThreadRunning $flags] {
        set state running
    }

    lappend scheduledProcs [list $id $state $proc $arg $flags $lastqueue $laststart $lastend $duration $next]
}

set rows ""

foreach s [_ns_stats.sortResults $scheduledProcs [expr $col - 1] $numericSort $reverseSort] {
    set id          [lindex $s 0]
    set state       [lindex $s 1]
    set flags       [join [_ns_stats.getSchedFlagTypes [lindex $s 4]] "<br>"]
    set next        [_ns_stats.fmtTime [lindex $s 9]]
    set lastqueue   [_ns_stats.fmtTime [lindex $s 5]]
    set laststart   [_ns_stats.fmtTime [lindex $s 6]]
    set lastend     [_ns_stats.fmtTime [lindex $s 7]]
    set proc        [lindex $s 2]
    set arg         [lindex $s 3]
    set duration    [_ns_stats.fmtSeconds [lindex $s 8]]
   
    lappend rows [list $id $state $proc $arg $flags $lastqueue $laststart $lastend $duration $next]
}

set colTitles [list ID Status Callback Data Flags "Last Queue" "Last Start" "Last End" Duration "Next Run"]

ns_adp_include results.inc $col $colTitles sched.adp $rows $reverseSort
%>  
