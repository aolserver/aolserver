proc test {script} {
    set ::assertionCount 0
    if {[set res [catch [list eval $script] msg]]} {
        ns_adp_puts -nonewline "FAIL\n$::assertionCount assertions\n$msg"
    } else {
        ns_adp_puts -nonewline "PASS\n$::assertionCount assertions"
    }
}

proc assert {true message} {
    if {!$true} {
        error $message
    }
}

proc assertEquals {expected actual {message ""}} {
    if {![info exists ::assertionCount]} {
        set ::assertionCount 0
    }
    incr ::assertionCount
    if {![string length $message]} {
        set message "no description provided"
    }
    set message "(#$::assertionCount) $message"
    assert [string equal $expected $actual] \
        "$message\n  Expected: $expected\n    Actual: $actual"
}
