<%
set log ""

catch {
    set f [open [ns_info log]]
    seek $f 0 end
    set n [expr [tell $f] -4000]
    
    if {$n < 0} {
        set n 4000
    }
    
    seek $f $n
    gets $f
    set log [ns_quotehtml [read $f]]
    close $f
}

ns_adp_puts "<font size=2><pre>$log</pre></font>"
%>
