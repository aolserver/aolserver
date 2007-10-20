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
# $Header: /Users/dossy/Desktop/cvs/aolserver/tcl/util.tcl,v 1.5 2007/10/20 16:39:21 gneumann Exp $
#

#
# util.tcl --
#
#	Various utilitiy procs.
#


# ns_localtime returns a time as a list of elements, and ns_parsetime
# returns one of those elements

proc ns_parsetime {option time} {
    set parts {sec min hour mday mon year wday yday isdst}
    set pos [lsearch $parts $option]
    if {$pos == -1} {
	error "Incorrect option to ns_parsetime: \"$option\" Should be\
               one of \"$parts\""
    }
    return [lindex $time $pos]
}

# ns_findset returns a set with a given name from a list.

proc ns_findset {sets name} {
    foreach set $sets {
	if {[ns_set name $set] == $name} {
	    return $set
	}
    }
    return ""
}

# getformdata - make sure an HTML FORM was sent with the request.
proc getformdata {conn formVar} {
    upvar $formVar form
    set form [ns_conn form $conn]
    if {$form eq ""} {
	ns_returnbadrequest $conn "Missing HTML FORM data"
	return 0
    }
    return 1
}

proc ns_paren {val} {
    if {$val ne ""} {
	return "($val)"
    } else {
	return ""
    }
}

proc Paren {val} {
    return [ns_paren $val]
}

proc issmallint {value} {
    return [ns_issmallint $value]
}

proc ns_issmallint {value} {
    return [expr {[regexp {^[0-9]+$} $value] && [string length $value] <= 6}]
}

## Special thanks to Brian Tivol at Hearst New Media Center and MIT
## for providing the core of this code.

proc ns_formvalueput {htmlpiece dataname datavalue} {

    set newhtml ""

    while {$htmlpiece ne ""} {
	if {[string index $htmlpiece 0] == "<"} {
	    regexp {<([^>]*)>(.*)} $htmlpiece m tag htmlpiece
	    set tag [string trim $tag]
	    set CAPTAG [string toupper $tag]

	    switch -regexp $CAPTAG {

		{^INPUT} {
		    if {[regexp {TYPE=("IMAGE"|"SUBMIT"|"RESET"|IMAGE|SUBMIT|RESET)} $CAPTAG]} {
			append newhtml <$tag>
			
		    } elseif {[regexp {TYPE=("CHECKBOX"|CHECKBOX|"RADIO"|RADIO)} $CAPTAG]} {
			
			set name [ns_tagelement $tag NAME]

			if {$name == $dataname} {

			    set value [ns_tagelement $tag VALUE]

			    regsub -all -nocase { *CHECKED} $tag {} tag

			    if {$value == $datavalue} {
				append tag " CHECKED"
			    }
			}
			append newhtml <$tag>

		    } else {

			## If it's an INPUT TYPE that hasn't been covered
			#  (text, password, hidden, other (defaults to text))
			## then we add/replace the VALUE tag
			
			set name [ns_tagelement $tag NAME]
			
			if {$name == $dataname} {
			    ns_tagelementset tag VALUE $datavalue
			}
			append newhtml <$tag>
		    }
		}

		{^TEXTAREA} {

		    ###
		    #   Fill in the middle of this tag
		    ###

		    set name [ns_tagelement $tag NAME]
		    
		    if {$name == $dataname} {
			while {![regexp -nocase {^<( *)/TEXTAREA} $htmlpiece]} {
			    regexp {^.[^<]*(.*)} $htmlpiece m htmlpiece
			}
			append newhtml <$tag>$datavalue
		    } else {
			append newhtml <$tag>
		    }
		}
		
		{^SELECT} {

		    ### Set flags so OPTION and /SELECT know what to look for:
		    #   snam is the variable name, sflg is 1 if nothing's
		    ### been added, smul is 1 if it's MULTIPLE selection


		    if {[ns_tagelement $tag NAME] == $dataname} {
			set inkeyselect 1
			set addoption 1
		    } else {
			set inkeyselect 0
			set addoption 0
		    }

		    append newhtml <$tag>
		}

		{^OPTION} {
		    
		    ###
		    #   Find the value for this
		    ###

		    if {$inkeyselect} {

			regsub -all -nocase { *SELECTED} $tag {} tag

			set value [ns_tagelement $tag VALUE]

			regexp {^([^<]*)(.*)} $htmlpiece m txt htmlpiece

			if {$value eq ""} {
			    set value [string trim $txt]
			}

			if {$value == $datavalue} {
			    append tag " SELECTED"
			    set addoption 0
			}
			append newhtml <$tag>$txt
		    } else {
			append newhtml <$tag>
		    }
		}

		{^/SELECT} {
		    
		    ###
		    #   Do we need to add to the end?
		    ###
		    
		    if {$inkeyselect && $addoption} {
			append newhtml "<option selected>$datavalue<$tag>"
		    } else {
			append newhtml <$tag>
		    }
		    set inkeyselect 0
		    set addoption 0
		}
		
		{default} {
		    append newhtml <$tag>
		}
	    }

	} else {
	    regexp {([^<]*)(.*)} $htmlpiece m brandnew htmlpiece
	    append newhtml $brandnew
	}
    }
    return $newhtml
}


proc ns_tagelement {tag key} {
    set qq {"([^"]*)"}                ; # Matches what's in quotes
    set pp {([^ >]*)}                 ; # Matches a word (mind yer pp and qq)
    
    if {[regexp -nocase "$key *= *$qq" $tag m name]} {}\
	    elseif {[regexp -nocase "$key *= *$pp" $tag m name]} {}\
	    else {set name ""}
    return $name
}


# Assumes that the final ">" in the tag has been removed, and
# leaves it removed

proc ns_tagelementset {tagvar key value} {

    upvar $tagvar tag

    set qq {"([^"]*)"}                ; # Matches what's in quotes
    set pp {([^ >]*)}                 ; # Matches a word (mind yer pp and qq)
    
    regsub -all -nocase "$key=$qq" $tag {} tag
    regsub -all -nocase "$key *= *$pp" $tag {} tag
    append tag " value=\"$value\""
}


# sorts a list of pairs based on the first value in each pair

proc _ns_paircmp {pair1 pair2} {
    if {[lindex $pair1 0] > [lindex $pair2 0]} {
	return 1
    } elseif {[lindex $pair1 0] < [lindex $pair2 0]} {
	return -1
    } else {
	return 0
    }
}

# ns_htmlselect ?-multi? ?-sort? ?-labels labels? key values ?selecteddata?

proc ns_htmlselect args {

    set multi 0
    set sort 0
    set labels {}
    while {[string index [lindex $args 0] 0] == "-"} {
	if {[lindex $args 0] eq "-multi"} {
	    set multi 1
	    set args [lreplace $args 0 0]
	}
	if {[lindex $args 0] eq "-sort"} {
	    set sort 1
	    set args [lreplace $args 0 0]
	}
	if {[lindex $args 0] eq "-labels"} {
	    set labels [lindex $args 1]
	    set args [lreplace $args 0 1]
	}
    }
    
    set key [lindex $args 0]
    set values [lindex $args 1]
    
    if {[llength $args] == 3} {
	set selecteddata [lindex $args 2]
    } else {
	set selecteddata ""
    }
    
    set select "<SELECT NAME=$key"
    if {$multi == 1} {
	set size [llength $values]
	if {$size > 5} {
	    set size 5
	}
	append select " MULTIPLE SIZE=$size"
    } else {
	if {[llength $values] > 25} {
	    append select " SIZE=5"
	}
    }
    append select ">\n"
    set len [llength $values]
    set lvpairs {}
    for {set i 0} {$i < $len} {incr i} {
	if {$labels eq ""} {
	    set label [lindex $values $i]
	} else {
	    set label [lindex $labels $i]
	}
	regsub -all "\"" $label "" label
	lappend lvpairs [list  $label [lindex $values $i]]
    }
    if {$sort} {
	set lvpairs [lsort -command _ns_paircmp -increasing $lvpairs]
    }
    foreach lvpair $lvpairs {
	append select "<OPTION VALUE=\"[lindex $lvpair 1]\""
	if {[lsearch $selecteddata [lindex $lvpair 1]] >= 0} {
	    append select " SELECTED"
	}
	append select ">[lindex $lvpair 0]\n"
    }
    append select "</SELECT>"

    return $select
}

proc _ns_fillinmailtemplate {templatebody row} {
    set rowsize [ns_set size $row]
    for {set i 0} {$i < $rowsize} {incr i} {
	set key "#[ns_set key $row $i]#"
	regsub -all "&" [ns_set value $row $i] {\\\&} value
	regsub -all $key $templatebody $value templatebody
    }
    return $templatebody
}


proc ns_setexpires args {
    # skip over the optional connId parameter: just use the last arg
    set secondsarg [expr {[llength $args] - 1}]

    ns_set update [ns_conn outputheaders] Expires \
	    [ns_httptime [expr {[lindex $args $secondsarg] + [ns_time]}]]
}


proc ns_browsermatch args {
    # skip over the optional connId parameter: just use the last arg
    set globarg [expr {[llength $args] - 1}]

    return [string match [lindex $args $globarg]  \
	    [ns_set iget [ns_conn headers] user-agent]]
}


proc ns_set_precision {precision} {
    global tcl_precision
    set tcl_precision $precision
}


proc ns_updateheader {key value} {
    ns_set update [ns_conn outputheaders] $key $value
}
