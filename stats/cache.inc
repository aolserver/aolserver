<%
set col         [ns_queryget col 1]
set reverseSort [ns_queryget reversesort 1]

set numericSort 1

if {$col == 1} {
    set numericSort 0
}

set results ""

foreach cache [ns_cache_names] {
    set t [ns_cache_stats $cache]
    
    scan [ns_cache_size $cache] "%d %d" M N
    scan $t "entries: %d  flushed: %d  hits: %d  misses: %d  hitrate: %d" e f h m p
    
    lappend results [list $cache $M $N $e $f $h $m "$p%"]
}

set colTitles   [list Cache Max Current Entries Flushes Hits Misses "Hit Rate"]
set rows        [_ns_stats.sortResults $results [expr $col - 1] $numericSort $reverseSort]

ns_adp_include results.inc $col $colTitles cache.adp $rows $reverseSort
%>
