#
# The contents of this file are subject to the AOLserver Public License
# Version 1.1 (the "License"); you may not use this file except in
# compliance with the License. You may obtain a copy of the License at
# http://aolserver.com/.
#
# Software distributed under the License is distributed on an "AS IS"
# basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
# the License for the specific language governing rights and limitations
# under the License.
#
# The Original Code is AOLserver Code and related documentation
# distributed by AOL.
# 
# The Initial Developer of the Original Code is America Online,
# Inc. Portions created by AOL are Copyright (C) 1999 America Online,
# Inc. All Rights Reserved.
#
# Alternatively, the contents of this file may be used under the terms
# of the GNU General Public License (the "GPL"), in which case the
# provisions of GPL are applicable instead of those above.  If you wish
# to allow use of your version of this file only under the terms of the
# GPL and not to allow others to use your version of this file under the
# License, indicate your decision by deleting the provisions above and
# replace them with the notice and other provisions required by the GPL.
# If you do not delete the provisions above, a recipient may use your
# version of this file under either the License or the GPL.
#

#
# $Header: /Users/dossy/Desktop/cvs/aolserver/tcl/nsdb.tcl,v 1.5 2009/01/19 12:21:08 gneumann Exp $
#

#
# nsdb.tcl --
#
#	Database services utils.
#

nsv_set _nsdb months [list January February March April May June \
	July August September October November December]


#
# ns_dbquotename -
#
#	If name contains a space, then it is surrounded by double quotes.
#	This is useful for names in SQL statements that may contain spaces.
#

proc ns_dbquotename {name} {
    if {[string match "* *" $name]} {
	return "\"$name\""
    } else {
	return $name
    }   
}

#
# ns_dbquotevalue -
#
#	Prepares a value string for inclusion in an SQL statement:
#		"" is translated into NULL.
#		All values of any numeric type are left alone.
#		All other values are surrounded by single quotes and any
#		single quotes included in the value are escaped (ie. translated
#		into 2 single quotes). 

proc ns_dbquotevalue {value {type text}} {
    if {$value eq ""} {
	return "NULL"
    }
    if {$type eq "decimal" \
	    || $type eq "double" \
	    || $type eq "integer" \
	    || $type eq "int" \
	    || $type eq "real" \
	    || $type eq "smallint" \
	    || $type eq "bigint" \
	    || $type eq "bit" \
	    || $type eq "float" \
	    || $type eq "numeric" \
	    || $type eq "tinyint"} {
	return $value
    }
    regsub -all "'" $value "''" value
    return "'$value'"
}

#
# ns_localsqltimestamp -
#
#	Return an SQL string for the current time.
#

proc ns_localsqltimestamp {} {
    set time [ns_localtime]

    return [format "%04d-%02d-%02d %02d:%02d:%02d" \
	    [expr {[ns_parsetime year $time] + 1900}] \
	    [expr {[ns_parsetime mon $time] + 1}] \
	    [ns_parsetime mday $time] \
	    [ns_parsetime hour $time] \
	    [ns_parsetime min $time] \
	    [ns_parsetime sec $time]]
}

#
# ns_parsesqldate -
#
#	Parse and SQL date string fro month, day, or year.
#

proc ns_parsesqldate {opt sqldate} {
    scan $sqldate "%04d-%02d-%02d" year month day

    switch $opt {
	month {return [lindex [nsv_get _nsdb months] [expr {$month - 1}]]}
	day {return $day}
	year {return $year}
	default {error "Unknown option \"$opt\": should be year, month or day"}
    }
}

#
# ns_parsesqltime -
#
#	Parse and SQL timestamp string for time or ampm spec.
#

proc ns_parsesqltime {opt sqltime} {

    if {[scan $sqltime "%02d:%02d:%02d" hours minutes seconds] == 2} {
	set seconds 0
    }

    switch $opt {
	time {
	    if {$hours == 0} {
		set hours 12
	    } elseif {$hours > 12} {
		set hours [incr hours -12]
	    }
	    if {$seconds == 0} {
		return [format "%d:%02d" $hours $minutes]
	    } else {
		return [format "%d:%02d:%02d" $hours $minutes $seconds]
	    }
	}
	ampm {
	    if {$hours < 12} {
		return AM
	    } else {
		return PM
	    }
	}

	default {error "Unknown command \"$opt\": should be time or ampm"}
    }
}

#
# ns_parsesqltimestamp --
#
#	Parse and SQL timestamp string for month, day, year, time,
#	or ampm spec.
#

proc ns_parsesqltimestamp {opt sqltimestamp} {

    switch $opt {
	month -
	day -
	year {return [ns_parsesqldate $opt [lindex [split $sqltimestamp " "] 0]]}
	time -
	ampm {return [ns_parsesqltime $opt [lindex [split $sqltimestamp " "] 1]]}
	default {error "Unknown command \"$opt\": should be month, day, year, time or ampm"}
    }
}

#
# ns_buildsqltime -
#
#	Create an SQL timestamp.
#

proc ns_buildsqltime {time ampm} {

    if {"" eq $time && "" eq $ampm} {
	return ""
    }

    if {"" eq $time || "" eq $ampm} {
	error "Invalid time: $time $ampm"
    }
    set seconds 0
    set num [scan $time "%d:%d:%d" hours minutes seconds]

    if {$num < 2 || $num > 3 \
	    || $hours < 1 || $hours > 12 \
	    || $minutes < 0 || $minutes > 59 \
	    || $seconds < 0 || $seconds > 61} {
	error "Invalid time: $time $ampm"
    }

    if {$ampm eq "AM"} {
	if {$hours == 12} {
	    set hours 0
	}
    } elseif {$ampm eq "PM"} {
	if {$hours != 12} {
	    incr hours 12
	}
    } else {
	error "Invalid time: $time $ampm"
    }

    return [format  "%02d:%02d:%02d" $hours $minutes $seconds]
}

#
# ns_buildsqldate -
#
#	Create and SQL date string.
#

proc ns_buildsqldate {month day year} {
    if {"" eq $month \
	    && "" eq $day \
	    && "" eq $year} {
	return ""
    }

    if {![ns_issmallint $month]} {
	set month [expr {[lsearch [nsv_get _nsdb months] $month] + 1}]
    }

    if {"" eq $month \
	    || "" eq $day \
	    || "" eq $year \
	    || $month < 1 || $month > 12 \
	    || $day < 1 || $day > 31 \
	    || $year < 1\
            || ($month == 2 && $day > 29)\
            || (($year % 4) != 0 && $month == 2 && $day > 28) \
            || ($month == 4 && $day > 30)\
            || ($month == 6 && $day > 30)\
            || ($month == 9 && $day > 30)\
            || ($month == 11 && $day > 30) } {
	error "Invalid date: $month $day $year"
    }

    return [format "%04d-%02d-%02d" $year $month $day]
}

#
# ns_buildsqltimestamp -
#
#	Create and SQL timestamp string.
#

proc ns_buildsqltimestamp {month day year time ampm} {
    set date [ns_buildsqldate $month $day $year]
    set time [ns_buildsqltime $time $ampm]

    if {"" eq $date || "" eq $time} {
	return ""
    }

    return "$date $time"
}

#
# ns_writecsv -
#
#	Write an SQL table to an open file in csv format.
#

proc ns_writecsv {datafp db table {header 1}} {

    set row [ns_db select $db "select * from [ns_dbquotename $table]"]
    set rowsize [ns_set size $row]
    if {$header} {
	regsub -all "\"" [ns_set key $row 0] "\"\"" value
	puts -nonewline $datafp "\"$value\""
	for {set i 1} {$i < $rowsize} {incr i} {
	    regsub -all "\"" [ns_set key $row $i] "\"\"" value
	    puts -nonewline $datafp ",\"$value\""
	}
	puts -nonewline $datafp "\r\n"
    }
    while {[ns_db getrow $db $row]} {
	regsub -all \" [ns_set value $row 0] "\"\"" value
	puts -nonewline $datafp "\"$value\""
	for {set i 1} {$i < $rowsize} {incr i} {
	    regsub -all \" [ns_set value $row $i] "\"\"" value
	    puts -nonewline $datafp ",\"$value\""
	}
	puts -nonewline $datafp "\r\n"
    }
}
