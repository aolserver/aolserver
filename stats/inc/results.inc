<%
set selectedColNum  [ns_adp_argv 1]
set colTitles       [ns_adp_argv 2]
set colUrl          [ns_adp_argv 3]
set rows            [ns_adp_argv 4]
set reverseSort     [ns_adp_argv 5]
set colAlignment    [ns_adp_argv 6]

set numCols         [llength $colTitles]

for {set colNum 1} {$colNum < [expr $numCols + 1]} {incr colNum} {
    if {$colNum == $selectedColNum} {
        set colHdrColor($colNum)        "#666666"
        set colHdrFontColor($colNum)    "#ffffff"
        set colColor($colNum)           "#ececec"
    } else {
        set colHdrColor($colNum)        "#999999"
        set colHdrFontColor($colNum)    "#ffffff"
        set colColor($colNum)           "#ffffff"
    }
}
%>

<table border=0 cellpadding=0 cellspacing=1 bgcolor=#cccccc>
<tr>
    <td valign=middle align=center>
    <table border=0 cellpadding=4 cellspacing=1 width="100%">
    <tr>
        
        <%
        set i 1
        foreach title $colTitles {
            set url $colUrl
            
            if {$i == $selectedColNum} {
                if $reverseSort {
                    append url "?reversesort=0"
                } else {
                    append url "?reversesort=1"
                }
            } else {
                append url "?reversesort=$reverseSort"
            }
            
            set colAlign "left"
        
            if [llength $colAlignment] {
                set align [lindex $colAlignment [expr $i - 1]]

                if [string length $align] {
                    set colAlign $align
                }
            }
            
            ns_adp_puts "<td valign=middle align=$colAlign bgcolor=$colHdrColor($i)><a href=$url&col=$i><font color=$colHdrFontColor($i)>$title</font></a></td>"
        
            incr i
        }
        %>
        
    </tr>
    
    <%
    foreach row $rows {
        set i 1
        
        ns_adp_puts "<tr>"
        
        foreach column $row {
            set colAlign "left"
        
            if [llength $colAlignment] {
                set align [lindex $colAlignment [expr $i - 1]]

                if [string length $align] {
                    set colAlign $align
                }
            }
            
            ns_adp_puts "<td bgcolor=$colColor($i) valign=top align=$colAlign>$column</td>"
        
            incr i
        }
        
        ns_adp_puts "</tr>"
    }
    %>
    
    </table>
    </td>
</tr>
</table>
    


        
    
