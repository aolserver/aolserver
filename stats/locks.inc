<%
set col         [ns_queryget col 1]
set reverseSort [ns_queryget reversesort 1]

set numericSort 1
set colTitles   [list Name Owner ID Locks Busy Contention]
set rows        ""

if {$col == 1 || $col == 2} {
    set numericSort 0
}

set results ""

foreach l [ns_info locks] {
    set name    [lindex $l 0]
    set owner   [lindex $l 1]
    set id      [lindex $l 2]
    set nlock   [lindex $l 3]
    set nbusy   [lindex $l 4]
    
    if {$nbusy == 0} {
        set contention 0.0
    } else {
        set contention [expr double($nbusy*100.0/$nlock)]
    }
    
    lappend results [list $name $owner $id $nlock $nbusy $contention]
}

foreach result [_ns_stats.sortResults $results [expr $col - 1] $numericSort $reverseSort] {
    set name        [lindex $result 0]
    set owner       [lindex $result 1]
    set id          [lindex $result 2]
    set nlock       [lindex $result 3]
    set nbusy       [lindex $result 4]
    set contention  [lindex $result 5]
    
    if {$contention < 2} {
        set color "black"
    } elseif {$contention < 5} {
        set color "orange"
    } else {
        set color "red"
    }
    
    lappend rows [list "<font color=$color>$name</font>" "<font color=$color>$owner</font>" "<font color=$color>$id</font>" "<font color=$color>$nlock</font>" "<font color=$color>$nbusy</font>" "<font color=$color>$contention</font>"]
}

ns_adp_include results.inc $col $colTitles locks.adp $rows $reverseSort
%>
