<%
set type [ns_adp_argv 1]
set msg [ns_adp_argv 2]

switch $type {
    "error" {
        set color "red"
    }
    "warning" {
        set color "orange"
    }
    "success" {
        set color "green"
    }
    default {
        set color "black"
    }
}

ns_adp_puts "<font color=$color><b>[string toupper $type]: $msg</b></font>"
%>
