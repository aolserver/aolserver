<center><b>ns_job unit tests</b></center>

<%

set maxThreads 5
set queueName test_queue_0


# ------------------------------------------------------------------------------
# Return code - succcess.
#
proc rc.success {} {
    return 0
}

# ------------------------------------------------------------------------------
# Return code - failure
#
proc rc.failure {} {
    return 1
}

# ------------------------------------------------------------------------------
# boolean true
#
proc true {} {
    return 1
}

# ------------------------------------------------------------------------------
# boolean false
#
proc false {} {
    return 0
}

# ------------------------------------------------------------------------------
# Log the message and write it to the page.
#
proc outputMsg {msg} {
    ns_log error $msg
    ns_adp_puts $msg
}


# ------------------------------------------------------------------------------
# Busy wait proc - used to test threads.
# 
ns_eval proc foo {value} {
    set x 0
    while {$x < 10000} {
        incr x
    }
    return $value
}


# ------------------------------------------------------------------------------
# Long Busy wait proc - used to test threads.
# 
ns_eval proc fooLong {value} {
    set x 0
    while {$x < 10000000} {
        incr x
    }
    return $value
}



# ------------------------------------------------------------------------------
# Check if the specified queue exists.
#
proc queueExists {queueName} {

    set queues [ns_job queues]
    if {[lsearch -exact $queues $queueName] == -1} {
        return [false]
    }
    return [true]
}

# ------------------------------------------------------------------------------
# Create the thread pool queue. Also test the create failure cases.
#
proc createQueue {queueName} {

    global maxThreads

    if {[queueExists $queueName]} {
        #
        # Trying to create a queue again should throw an error.
        #
        if {[catch {
            ns_job create $queueName $maxThreads
            outputMsg "Failure: Create queue should have thrown an erorr.<br>"
            return [rc.failure]
        } err]} {
            return [rc.success]
        }
    } else {
        outputMsg "Creating thread pool queue with max thread: $maxThreads ...<br>"
        ns_job create $queueName $maxThreads
        outputMsg "Done creating thread pool queue.<br><br>"

        if {![queueExists $queueName]} {
            outputMsg "Failure: Failed to create queue.<br>"
            return [rc.failure]
        }
    }
    return [rc.success]
}


# ------------------------------------------------------------------------------
# Unit Test #1
#
proc ns_job_unit_test_1 {testNum} {

    global queueName

    set count 10

    outputMsg "<br>Running test # $testNum.<br><br>"

    #
    # Enqueue jobs
    #
    outputMsg "Enqueuing jobs ...<br>"
    if {[catch {
        for {set i 0} {$i < $count} {incr i} {
            lappend job_list [ns_job queue $queueName "foo $i"]
        }
    } err]} {
	global errorInfo
	set savedInfo $errorInfo
	outputMsg "Failed to enqueue jobs. Error Info: $errorInfo"
        return [rc.failure]
    }
    outputMsg "Done enqueuing jobs.<br><br>"
    
    #
    # Wait for each job to complete.
    #
    outputMsg "Wait for each job to complete ...<br><br><blockquote>"
    if {[catch {
        foreach job_id $job_list {
            outputMsg "Job Id: $job_id &nbsp;&nbsp; Job Result: [ns_job wait $queueName $job_id]<br>"
        }
    } err]} {
        global errorInfo
        set savedInfo $errorInfo
        outputMsg "Failed to wait on job. Error Info: $errorInfo"
        return [rc.failure]
    }
    outputMsg "</blockquote><br>Done removing the jobs.<br><br>"
    outputMsg "<br>Test # $testNum Complete.<br>"

    return [rc.success]
}


# ------------------------------------------------------------------------------
# Unit Test #2
#
proc ns_job_unit_test_2 {testNum} {

    global queueName

    outputMsg "<br>Running test # $testNum.<br><br>"
    #
    # Enqueue jobs
    #
    outputMsg "Enqueuing jobs ...<br>"
    if {[catch {
        for {set i 0} {$i < 10} {incr i} {
            lappend job_list [ns_job queue $queueName "foo $i"]
        }
    } err]} {
	global errorInfo
	set savedInfo $errorInfo
	outputMsg "Failed to enqueue jobs. Error Info: $errorInfo"
        return [rc.failure]
    }
    outputMsg "Done enqueuing jobs.<br><br>"
    
    #
    # Watch jobs progress.
    #
    outputMsg "Watch jobs progress ...<br><br>"
    set done 0
    while {$done == 0} {
        set done 1

        outputMsg "
<br>
<table border=1>
<tr>
  <th>ID</th><th>State</th><th>Type</th><th>Request</th><th>Script</th><th>Results</th><th>TIME</th>
</tr>
"

        if {[catch {
            foreach job [ns_job job_list $queueName] {
                array set jobArr $job
                outputMsg "
<tr>
  <td>$jobArr(ID)</td><td>$jobArr(STATE)</td><td>$jobArr(TYPE)</td><td>$jobArr(REQ)</td><td>$jobArr(SCRIPT)</td><td>$jobArr(RESULTS)</td><td>$jobArr(TIME)</td>
</tr>
"
                #
                # Keep watching if any jobs are queued or running.
                #
                if {($jobArr(STATE) == "JOB_SCHEDULED") ||
                    ($jobArr(STATE) == "JOB_RUNNING") } {
                    set done 0
                }   
            }
        } err]} {
            global errorInfo
            set savedInfo $errorInfo
            outputMsg "Failed to get job list info. Error Info: $errorInfo"
            return [rc.failure]
        }

        outputMsg "
</table>
<br>
"
    }

    #
    # Remove the jobs from the queue.
    #
    outputMsg "<br>The Jobs have completed. Remove the from the queue ...<br><br>"
    if {[catch {
        foreach job_id $job_list {
            outputMsg "Job Id: $job_id   Job Result: [ns_job wait $queueName $job_id]<br>"
        }
    } err]} {
        global errorInfo
        set savedInfo $errorInfo
        outputMsg "Failed to wait on job. Error Info: $errorInfo"
        return [rc.failure]
    }
    outputMsg "<br>Done removing the jobs.<br><br>"
    outputMsg "<br>Test # $testNum Complete.<br>"
    
    return [rc.success]
}


# ------------------------------------------------------------------------------
# Unit Test #3
#
proc ns_job_unit_test_3 {testNum} {

    global queueName

    outputMsg "<br>Running test # $testNum.<br><br>"
    #
    # Enqueue jobs
    #
    outputMsg "Enqueuing jobs ...<br>"
    if {[catch {
        for {set i 0} {$i < 10} {incr i} {
            lappend job_list [ns_job queue $queueName "foo $i"]
        }
    } err]} {
	global errorInfo
	set savedInfo $errorInfo
	outputMsg "Failed to enqueue jobs. Error Info: $errorInfo"
        return [rc.failure]
    }
    outputMsg "Done enqueuing jobs.<br><br>"
    
    #
    # Wait for any job to complete.
    #
    outputMsg "Wait for any job to complete...<br>"
    if {[catch {
        ns_job wait_any $queueName

    } err]} {
        outputMsg "wait_any failed. Error Info: $errorInfo"
        return [rc.failure]
    }
    outputMsg "Done waiting for any job to complete.<br><br>"

    #
    # Remove the jobs from the queue.
    #
    outputMsg "<br>Remove all the jobs from the queue ...<br><br>"
    if {[catch {
        foreach job_id $job_list {
            outputMsg "Job Id: $job_id   Job Result: [ns_job wait $queueName $job_id]<br>"
        }
    } err]} {
        global errorInfo
        set savedInfo $errorInfo
        outputMsg "Failed to wait on job. Error Info: $errorInfo<br>"
        return [rc.failure]
    }
    outputMsg "<br>Done removing all the jobs.<br><br>"

    #
    # Wait for any job to complete.
    #
    outputMsg "Wait again for any job (this should not hang) ...<br>"
    if {[catch {
        ns_job wait_any $queueName

    } err]} {
        outputMsg "wait_any failed. Error Info: $errorInfo"
        return [rc.failure]
    }
    outputMsg "Done waiting for any job to complete.<br><br>"

    outputMsg "<br>Test # $testNum Complete.<br>"
    return [rc.success]
}


# ------------------------------------------------------------------------------
# Unit Test #4
#
proc ns_job_unit_test_4 {testNum} {

    global queueName

    outputMsg "<br>Running test # $testNum.<br><br>"
    #
    # Enqueue jobs
    #
    outputMsg "Enqueuing detached jobs ...<br>"
    if {[catch {
        for {set i 0} {$i < 10} {incr i} {
            lappend job_list [ns_job queue -detached $queueName "foo $i"]
        }
    } err]} {
	global errorInfo
	set savedInfo $errorInfo
	outputMsg "Failed to enqueue jobs. Error Info: $errorInfo<br>"
        return [rc.failure]
    }
    outputMsg "Done enqueuing detached jobs.<br><br>"
    
    #
    # Watch jobs progress.
    #
    outputMsg "Watch jobs progress ...<br><br>"
    set done 0
    while {$done == 0} {
        set done 1

        outputMsg "
<br>
<table border=1>
<tr>
  <th>ID</th><th>State</th><th>Type</th><th>Request</th><th>Script</th><th>Results</th><th>TIME</th>
</tr>
"

        if {[catch {
            foreach job [ns_job job_list $queueName] {
                array set jobArr $job
                outputMsg "
<tr>
  <td>$jobArr(ID)</td><td>$jobArr(STATE)</td><td>$jobArr(TYPE)</td><td>$jobArr(REQ)</td><td>$jobArr(SCRIPT)</td><td>$jobArr(RESULTS)</td><td>$jobArr(TIME)</td>
</tr>
"
                #
                # Keep watching if any jobs are queued or running.
                #
                if {($jobArr(STATE) == "JOB_SCHEDULED") ||
                    ($jobArr(STATE) == "JOB_RUNNING") } {
                    set done 0
                }   
            }
        } err]} {
            global errorInfo
            set savedInfo $errorInfo
            outputMsg "Failed to get job list info. Error Info: $errorInfo"
            return [rc.failure]
        }

        outputMsg "
</table>
<br>
"

    }

    #
    # Test wait
    #
    foreach job_id $job_list {
        if {[catch {
            ns_job wait $queueName $job_id
            outputMsg "Failure: Wait should not be allowed for detached jobs.<br>"
            return [rc.failure]
        } err]} {
            # wait should have thrown an error.
        }
    }

    outputMsg "<br>Test # $testNum Complete.<br>"
    return [rc.success]
}


# ------------------------------------------------------------------------------
# Unit Test #5
#
proc ns_job_unit_test_5 {testNum} {

    global queueName

    outputMsg "<br>Running test # $testNum.<br><br>"
    #
    # Enqueue jobs
    #
    outputMsg "Enqueuing jobs ...<br>"
    if {[catch {
        for {set i 0} {$i < 10} {incr i} {
            lappend job_list [ns_job queue $queueName "foo $i"]
        }
    } err]} {
	global errorInfo
	set savedInfo $errorInfo
	outputMsg "Failed to enqueue jobs. Error Info: $errorInfo<br>"
        return [rc.failure]
    }
    outputMsg "Done enqueuing jobs.<br><br>"
    
    #
    # Wait for any job to complete.
    #
    outputMsg "Wait for any job to complete...<br>"
    if {[catch {
        ns_job wait_any $queueName

    } err]} {
        outputMsg "wait_any failed. Error Info: $errorInfo"
        return [rc.failure]
    }
    outputMsg "Done waiting for any job to complete.<br><br>"


    #
    # Cancel all the jobs
    #
    outputMsg "Cancelling all the jobs...<br>"
    foreach job_id $job_list {
        if {[catch {
            ns_job cancel $queueName $job_id
        } err]} {
            outputMsg "Failed to cancel a job.<br>"
            return [rc.failure]
        }
    }
    outputMsg "Done cancelling all the jobs.<br><br>"

    #
    # Test wait
    #
    foreach job_id $job_list {
        if {[catch {
            ns_job wait $queueName $job_id
            outputMsg "Failure: Wait should not be allowed for cancelled jobs.<br>"
            return [rc.failure]
        } err]} {
            # wait should have thrown an error.
        }
    }


    #
    # Watch jobs progress.
    #
    outputMsg "Watch jobs progress ...<br><br>"
    set done 0
    while {($done == 0) && [llength [ns_job job_list $queueName]] } {
        set done 1

        outputMsg "
<br>
<table border=1>
<tr>
  <th>ID</th><th>State</th><th>Type</th><th>Request</th><th>Script</th><th>Results</th><th>TIME</th>
</tr>
"
        if {[catch {
            foreach job [ns_job job_list $queueName] {
                array set jobArr $job
                outputMsg "
<tr>
  <td>$jobArr(ID)</td><td>$jobArr(STATE)</td><td>$jobArr(TYPE)</td><td>$jobArr(REQ)</td><td>$jobArr(SCRIPT)</td><td>$jobArr(RESULTS)</td><td>$jobArr(TIME)</td>
</tr>
"
                #
                # Keep watching if any jobs are queued or running.
                #
                if {($jobArr(STATE) == "JOB_SCHEDULED") ||
                    ($jobArr(STATE) == "JOB_RUNNING") } {
                    set done 0
                }   
            }
        } err]} {
            global errorInfo
            set savedInfo $errorInfo
            outputMsg "Failed to get job list info. Error Info: $errorInfo"
            return [rc.failure]
        }

        outputMsg "
</table>
<br>
"
    }


    outputMsg "<br>Test # $testNum Complete.<br>"
    return [rc.success]
}


# ------------------------------------------------------------------------------
# Unit Test #6
#
proc ns_job_unit_test_6 {testNum} {

    global queueName

    outputMsg "<br>Running test # $testNum.<br><br>"

    #
    # Enqueue jobs
    #
    outputMsg "Enqueuing jobs ...<br>"
    if {[catch {
        for {set i 0} {$i < 10} {incr i} {
            lappend job_list [ns_job queue $queueName "fooLong $i"]
        }
    } err]} {
	global errorInfo
	set savedInfo $errorInfo
	outputMsg "Failed to enqueue jobs. Error Info: $errorInfo<br>"
        return [rc.failure]
    }
    outputMsg "Done enqueuing jobs.<br><br>"
    
    #
    # Wait for any job to complete.
    #
    outputMsg "Wait for any job to complete...<br>"
    if {[catch {
        ns_job wait_any -timeout 0 10 $queueName
        outputMsg "Failed to timeout call."
        return [rc.failure]
    } err]} {
        #
        # When a message times out, it throws an error.
        #
    }
    outputMsg "Done waiting for any job to complete.<br><br>"


    #
    # Cancel all the jobs
    #
    outputMsg "Cancelling all the jobs...<br>"
    foreach job_id $job_list {
        if {[catch {
            ns_job cancel $queueName $job_id
        } err]} {
            outputMsg "Failed to cancel a job.<br>"
            return [rc.failure]
        }
    }
    outputMsg "Done cancelling all the jobs.<br><br>"


    #
    # Enqueue jobs
    #
    outputMsg "Enqueuing jobs ...<br>"
    set job_list [list]
    if {[catch {
        for {set i 0} {$i < 10} {incr i} {
            lappend job_list [ns_job queue $queueName "fooLong $i"]
        }
    } err]} {
	global errorInfo
	set savedInfo $errorInfo
	outputMsg "Failed to enqueue jobs. Error Info: $errorInfo<br>"
        return [rc.failure]
    }
    outputMsg "Done enqueuing jobs.<br><br>"


    #
    # Test wait
    #
    foreach job_id $job_list {
        if {[catch {
            ns_job wait -timeout 0 10 $queueName $job_id
            outputMsg "Failed to timeout call."
            return [rc.failure]
        } err]} {
            # wait should have thrown an error.
        }
    }

    #
    # Cancel all the jobs
    #
    outputMsg "Cancelling all the jobs...<br>"
    foreach job_id $job_list {
        if {[catch {
            ns_job cancel $queueName $job_id
        } err]} {
            global errorInfo
            set savedInfo $errorInfo
            outputMsg "Failed to cancel a job. Error Info: $errorInfo<br>"
            return [rc.failure]
        }
    }
    outputMsg "Done cancelling all the jobs.<br><br>"

    outputMsg "<br>Test # $testNum Complete.<br>"
    return [rc.success]
}


   
# ------------------------------------------------------------------------------
# Get the test.
# 0 (zero) returned if no test is selected.
#
proc getTestNum {} {
    set queryOptions [ns_conn query]

    set testSelected 0

    foreach item $queryOptions {

        set pair [split $item "="]
        if {[llength $pair] != 2} {
            return $testSelected
        }
        set opt [lindex $pair 0]
        set value [lindex $pair 1]

        if {$opt == "testSelected"} {
            set testSelected $value
        }
    }
    return $testSelected
}


# ------------------------------------------------------------------------------
# Display menu
#
proc displayMenu {} {
    outputMsg "\
Select a test to run:<br>
<ul>
<li><a href=ns_job.adp?testSelected=1>Test \#1 - Basic funcationality</a>
<li><a href=ns_job.adp?testSelected=2>Test \#2 - List Jobs</a>
<li><a href=ns_job.adp?testSelected=3>Test \#3 - Wait Any</a>
<li><a href=ns_job.adp?testSelected=4>Test \#4 - Detached</a>
<li><a href=ns_job.adp?testSelected=5>Test \#5 - Cancel</a>
<li><a href=ns_job.adp?testSelected=6>Test \#6 - Timeout</a>
<li><a href=ns_job.adp>Menu Only</a>
</ul>
<br>
<hr>
<br>
"
}


# ------------------------------------------------------------------------------
# Display Queues
#
proc displayQueues {} {
    if {[catch {
         set queueList [ns_job queue_list]
     } err]} {
	global errorInfo
	set savedInfo $errorInfo
        outputMsg "Failed to get queue list. Error Info: $errorInfo<br>"
        return [rc.failure]
    }

        outputMsg "\
<br>
<table border=1>
<tr>
  <th>Name</th><th>MAX Threads</th><th>Number Threads</th><th>Number Idle</th><th>Request</th>
</tr>
"

    foreach queue $queueList {
        array set queueArr $queue
        
        outputMsg "
<tr>
  <td>$queueArr(NAME)</td><td>$queueArr(MAX_THREADS)</td><td>$queueArr(NUM_THREADS)</td><td>$queueArr(NUM_IDLE)</td><td>$queueArr(REQ)</td>
</tr>
"
    }

        outputMsg "\
</table>
<br>
<hr>
<br>
"
}



# ------------------------------------------------------------------------------
# Main
#

displayMenu

if {[createQueue $queueName] == [rc.failure]} {
    outputMsg "Failed to create queue"
    return [rc.failure]
}

displayQueues

set testNum [getTestNum]

switch -glob -- $testNum {

    0  {

    }
    1  {
        if {[ns_job_unit_test_1 $testNum] != [rc.success]} {
            outputMsg "<br>Test $testNum failed!<br>"
        }
    }
    2  {
        if {[ns_job_unit_test_2 $testNum] != [rc.success]} {
            outputMsg "<br>Test $testNum failed!<br>"
        }
    }
    3 {
        if {[ns_job_unit_test_3 $testNum] != [rc.success]} {
            outputMsg "<br>Test $testNum failed!<br>"
        }
    }
    4 {
        if {[ns_job_unit_test_4 $testNum] != [rc.success]} {
            outputMsg "<br>Test $testNum failed!<br>"
        }
    }
    5 {
        if {[ns_job_unit_test_5 $testNum] != [rc.success]} {
            outputMsg "<br>Test $testNum failed!<br>"
        }
    }
    6 {
        if {[ns_job_unit_test_6 $testNum] != [rc.success]} {
            outputMsg "<br>Test $testNum failed!<br>"
        }
    }
    default {
        outputMsg "Unknown option. $testNum"
    }
}

%>
