<%
set uptime [ns_info uptime]

if {$uptime < 60} {
    set uptime [format %.2d $uptime] 
} elseif {$uptime < 3600} {
    set mins [expr $uptime / 60]
    set secs [expr $uptime - ($mins * 60)]

    set uptime "[format %.2d $mins]:[format %.2d $secs]"
} else {
    set hours [expr $uptime / 3600]
    set mins [expr ($uptime - ($hours * 3600)) / 60]
    set secs [expr $uptime - (($hours * 3600) + ($mins * 60))]

    set uptime "${hours}:[format %.2d $mins]:[format %.2d $secs]"
}

set config ""

lappend config "Build Date" [ns_info builddate]
lappend config "Build Label" [ns_info label]
lappend config "Build Platform" [ns_info platform]
lappend config "Build Version" [ns_info version]
lappend config "Build Patch Level" [ns_info patchlevel]
lappend config "&nbsp;" "&nbsp;"
lappend config "Binary" [ns_info nsd]
lappend config "Process ID" [ns_info pid]
lappend config "Uptime" $uptime
lappend config "Host Name" [ns_info hostname]
lappend config "Address" [ns_info address]
lappend config "Server Config" [ns_info config]
lappend config "Server Log" [ns_info log]
lappend config "Access Log" [ns_accesslog file]
lappend config "&nbsp;" "&nbsp;"
lappend config "Tcl Version" [info tclversion]
lappend config "Tcl Patch Level" [info patchlevel]
lappend config "&nbsp;" "&nbsp;"
lappend config "Home Directory" [ns_info home]
lappend config "Page Root" [ns_info pageroot]
lappend config "Tcl Library" [ns_info tcllib]
%>

<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN"
        "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">

<html>
<head>
<title>Welcome to AOLserver</title> 
<style type="text/css">
    body { 
        font-family: verdana, arial; 
        font-size: 10pt; 
        color: #000; 
        background-color: #fff; 
    }
    a:link, a:visited, a:active {
        text-decoration: none;
        color: blue;
    }
    b { 
        font-style: bold; 
    }
    h1 { 
        font-family: verdana, arial; 
        font-style: bold; 
        font-size: 14pt;  
    }
    h2 { 
        font-family: verdana, arial; 
        font-style: bold; 
        font-size: 12pt;  
    }
    table {
        border: solid 1px #000000;
    }
    td {
        font-family: verdana, arial; 
        font-size: 10pt; 
        border-right: solid 1px #cccccc;
        border-bottom: solid 1px #cccccc;
    }
    p {
        font-family: verdana, arial;
        font-size: 12pt;
    }
    #nav{
        margin-top: 1em;
        margin-bottom: 0.5em;
    }
    #nav ul {
        background-color: #ececec;
        text-align: left;
        margin-left: 0;
        padding-left: 0;
        padding-top: 2px;
        padding-bottom: 2px;
        border-bottom: 1px solid #cccccc;
    }
    #nav li {
        font-family: verdana, arial;
        font-size: 10pt;
        list-style-type: none;
        padding: 0.25em 1em;
        border-left: 1px solid white;
        display: inline;
    }
    #nav li:first-child {
        border: none;
    }
</style>
</head>
<body>

<div id="nav">
<ul>
    <li><a href="http://www.aolserver.com/">AOLserver.com</a></li>
    <li><a href="http://panoptic.com/wiki/aolserver/">Wiki</a></li>
    <li><a href="http://panoptic.com/wiki/aolserver/Tcl%20API">Tcl API</a></li>
    <li><a href="http://aolserver.com/docs/devel/c/api">C API</a></li>
    <li><a href="http://aolserver.com/lists.php">Mailing Lists</a></li>
    <li>&nbsp;</li>
</ul>
</div>

<h1>AOLserver <%=[ns_info version]%></h1> 

<p>Congratulations, you have successfully installed AOLserver <%=[ns_info version]%>!</p> 

<h2>Configuration</h2>

<table cellpadding="3" cellspacing="0">
<tr>
    <td><b>Key</b></td>
    <td><b>Value</b></td>
<tr>

<%
foreach {key value} $config {
     ns_adp_puts "<tr><td>$key</td><td>$value</td></tr>"
}
%>

</table>

<h2>Loaded AOLserver Modules</h2>

<table cellpadding="3" cellspacing="0">
<tr>
    <td><b>Type</b></td>
    <td><b>Name</b></td>
    <td><b>Location</b></td>
</tr>

<%
set server [ns_info server]
set modSection [ns_configsection ns/server/$server/modules]
set tclDir [ns_info tcllib]
set binDir "[ns_info home]/bin"

foreach {name binary} [ns_set array $modSection] {
    if {[string match "tcl" $binary]} {
        set type "Tcl"
        set location "$tclDir/$name"
    } else {
        set type "C"
        set location "$binDir/$binary"
    }

    ns_adp_puts "<tr><td>$type</td><td>$name</td><td>$location</td></tr>"
}
%>

</table>

<%
set modules [info loaded]

if {[string length $modules]} {
    ns_adp_puts "\
    <h2>Loaded Tcl Modules</h2>

    <table cellpadding=\"3\" cellspacing=\"0\">
    <tr>
        <td><b>Name</b></td>
        <td><b>Location</b></td>
    </tr>"

    foreach module [info loaded] {
        foreach {binary name} $module {
            ns_adp_puts "<tr><td>$name</td><td>$binary</td></tr>"
        }
    }

    ns_adp_puts "</table>"
}
%>

</body>
</html>
