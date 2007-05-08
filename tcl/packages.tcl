set section "ns/server/[ns_info server]/packages"

if {[llength [set libraryList [ns_config $section librarylist]]]} {
    foreach library $libraryList {
        if {[lsearch -exact $::auto_path $library] == -1} {
            lappend ::auto_path $library
        }
    }
}

if {![llength [set packageList [ns_config $section packagelist]]]} {
    return
}

foreach package $packageList { 
    if {[catch {set version [ns_ictl package require $package]}]} {
        ns_log error $::errorInfo
        continue
    }

    ns_log debug "Package Loaded: ${package}: ${version}"
}

foreach command [list nsInit nsPostInit] {
    foreach package $packageList {
        set packageCommand "::${package}::${command}"
 
        if {![llength [info commands $packageCommand]]} {
            continue
        }

        ns_log debug "Running: ${packageCommand}"

        if {[catch {eval $packageCommand}]} {
            ns_log error $::errorInfo
        }
    }
}
