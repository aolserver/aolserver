if {[ns_config -bool ns/server/stats enabled] == 1} {
    nsv_set _ns_stats menu.cache    "Cache"
    nsv_set _ns_stats menu.commands "Tcl Commands"
    nsv_set _ns_stats menu.locks    "Locks"
    nsv_set _ns_stats menu.log      "Log"
    nsv_set _ns_stats menu.mempools "Memory Pools"
    nsv_set _ns_stats menu.process  "Process Information"
    nsv_set _ns_stats menu.sched    "Scheduled Procedures"
    nsv_set _ns_stats menu.threads  "Threads"
    nsv_set _ns_stats menu.url      "URL Stats"

	nsv_set _ns_stats thread.0      "NS_OK"
	nsv_set _ns_stats thread.-1     "NS_ERROR"
	nsv_set _ns_stats thread.-2     "NS_TIMEOUT"
	nsv_set _ns_stats thread.200    "NS_THREAD_MAXTLS"
	nsv_set _ns_stats thread.1      "NS_THREAD_DETACHED"
	nsv_set _ns_stats thread.2      "NS_THREAD_JOINED"
	nsv_set _ns_stats thread.4      "NS_THREAD_EXITED"
	nsv_set _ns_stats thread.32     "NS_THREAD_NAMESIZE"
	
	nsv_set _ns_stats sched.1       "thread"
	nsv_set _ns_stats sched.2       "once"
	nsv_set _ns_stats sched.4       "daily"
	nsv_set _ns_stats sched.8       "weekly"
	nsv_set _ns_stats sched.16      "paused"
	nsv_set _ns_stats sched.32      "running"
	
	nsv_set _ns_stats sched.thread  1
	nsv_set _ns_stats sched.once    2
	nsv_set _ns_stats sched.daily   4
	nsv_set _ns_stats sched.weekly  8
	nsv_set _ns_stats sched.paused  16
	nsv_set _ns_stats sched.running 32
	
	nsv_set _ns_stats config.user   [ns_config ns/server/stats user "aolserver"]
	nsv_set _ns_stats config.pass   [ns_config ns/server/stats password "stats"]
	nsv_set _ns_stats config.dir    [ns_config ns/server/stats dir "_stats"]
	
	ns_register_proc GET [nsv_get _ns_stats config.dir]/* _ns_stats
	
	proc _ns_stats proc {
		set user [nsv_get _ns_stats config.user]
		set pass [nsv_get _ns_stats config.pass]
		
		if {[ns_conn authuser] != $user || [ns_conn authpassword] != $pass} {
			ns_returnunauthorized
			
			return filter_return
		}
		
		set page [ns_conn urlv [expr [ns_conn urlc] - 1]]
		set dir [file dirname [ns_info config]]/stats
		
		if {![string match "*.adp" $page]} {
			set page "index.adp"
		}
		
		if {![file exists $dir/$page]} {
			ns_returnnotfound
				
			return filter_return
		}
		
		ns_return 200 text/html [ns_adp_parse -file $dir/$page]
				
		return filter_ok
	}
	 
	proc _ns_stats.getValue {key} {
		if [catch {
			set value [nsv_get _ns_stats $key]
		}] {
			return ""
		}
		
		return $value
	}
	
	proc _ns_stats.getThreadType {flag} {
		return [_ns_stats.getValue thread.$flag]
	}
	
	proc _ns_stats.getSchedType {flag} {
		return [_ns_stats.getValue sched.$flag]
	}
	
	proc _ns_stats.getSchedFlag {type} {
		return [_ns_stats.getValue sched.$type]
	}
	
	proc _ns_stats.isThreadSuspended {flags} {
		return [expr $flags & [_ns_stats.getSchedFlag paused]]
	}
	
	proc _ns_stats.isThreadRunning {flags} {
		return [expr $flags & [_ns_stats.getSchedFlag running]]
	}
	
	proc _ns_stats.getSchedFlagTypes {flags} {
		if [expr $flags & [_ns_stats.getSchedFlag once]] {
			set types "once"
		} else {
			set types "repeating"
		}
		
		if [expr $flags & [_ns_stats.getSchedFlag daily]] {
			lappend types "daily"
		}
		
		if [expr $flags & [_ns_stats.getSchedFlag weekly]] {
			lappend types "weekly"
		}
		
		if [expr $flags & [_ns_stats.getSchedFlag thread]] {
			lappend types "thread"
		}
		
		return $types
	}
	
	proc _ns_stats.fmtSeconds {seconds} {
		if {$seconds < 60} {
			return "${seconds} (s)"
		}
	 
		if {$seconds < 3600} {
			set mins [expr $seconds/60]
			set secs [expr $seconds - ($mins * 60)]
	 
			return "${mins}:${secs} (m:s)"
		}
	 
		set hours [expr $seconds/3600]
		set mins [expr ($seconds - ($hours * 3600))/60]
		set secs [expr $seconds - (($hours * 3600) + ($mins * 60))]
	 
		return "${hours}:${mins}:${secs} (h:m:s)"
	}
	
	proc _ns_stats.fmtTime {time} {
		if {$time < 0} {
		return "never"
		}
		
		return [clock format $time -format "%I:%M:%S %p on %m/%d/%Y"]
	}
	
	proc _ns_stats.sortResults {results field numeric {reverse 0}} {
		global _sortListTmp
		
		set _sortListTmp(field)     $field
		set _sortListTmp(numeric)   $numeric
		set _sortListTmp(reverse)   $reverse
		
		return [lsort -command _ns_stats.cmpField $results]
	}
	
	proc _ns_stats.cmpField {v1 v2} {
		global _sortListTmp
		
		set v1  [lindex $v1 $_sortListTmp(field)]
		set v2  [lindex $v2 $_sortListTmp(field)]
		
		if $_sortListTmp(numeric) {
			if $_sortListTmp(reverse) {
				set cmp [_ns_stats.cmpNumeric $v2 $v1]
			} else {
				set cmp [_ns_stats.cmpNumeric $v1 $v2]
			}
		} else {
			if $_sortListTmp(reverse) { 
				set cmp [string compare $v2 $v1]
			} else {
				set cmp [string compare $v1 $v2]
			}
		}
		
		return $cmp
	}
	
	proc _ns_stats.cmpNumeric {n1 n2} {
		if {$n1 < $n2} {
			return -1
		} elseif {$n1 > $n2} {
			return 1
		}
		
		return 0
	}
	
	proc _ns_stats.addMenuItem {shortName description} {
	    return [nsv_set _ns_stats menu.$shortName $description]
	}
 
        proc _ns_stats.getMenuItemDescription {shortName} {
            return [nsv_get _ns_stats menu.$shortName]
        }
}
