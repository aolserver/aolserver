<%
set dir [nsv_get _ns_stats config.dir]

ns_adp_include inc/header.inc

foreach key [lsort [nsv_array names _ns_stats menu.*]] {
    ns_adp_puts "o <a href=/$dir/[lindex [split $key "."] 1].adp>[nsv_get _ns_stats $key]</a><br>"
}

ns_adp_include inc/footer.inc
%>
