<center><b>ns_job unit tests</b></center>

<%

set maxThreads 5
set queueId test_queue_0


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
proc queueExists {queueId} {

    set queues [ns_job queues]
    if {[lsearch -exact $queues $queueId] == -1} {
        return [false]
    }
    return [true]
}

# ------------------------------------------------------------------------------
# Create the thread pool queue. Also test the create failure cases.
#
proc createQueue {queueId} {

    global maxThreads

    if {[queueExists $queueId]} {
        #
        # Trying to create a queue again should throw an error.
        #
        if {[catch {
            ns_job create -desc "Unit Test Queue \#1" $queueId $maxThreads
            outputMsg "Failure: Create queue should have thrown an erorr.<br>"
            return [rc.failure]
        } err]} {
            return [rc.success]
        }
    } else {
        outputMsg "Creating thread pool queue with max thread: $maxThreads ...<br>"
        ns_job create -desc "Unit Test Queue \#1" $queueId $maxThreads
        outputMsg "Done creating thread pool queue.<br><br>"

        if {![queueExists $queueId]} {
            outputMsg "Failure: Failed to create queue.<br>"
            return [rc.failure]
        }
    }
    return [rc.success]
}


# ------------------------------------------------------------------------------
# Watch queue jobs progress.
#
proc watchQueue {queueId jobList} {

    outputMsg "Watch $queueId jobs progress ...<br><br>"
    set done 0
    while {$done == 0} {
        set done 1


        if {[catch {
            set first [true]
            foreach job [ns_job job_list $queueId] {
                array set jobArr $job

                if {$first} {
                    outputMsg "
<br>
<table border=1>
<tr>
  <th>ID</th>
  <th>State</th>
  <th>Type</th>
  <th>Request</th>
  <th>Script</th>
  <th>Results</th>
  <th>TIME</th>
  <th>START_TIME</th>
</tr>
"
                    set first [false]
                }

                outputMsg "
<tr>
  <td>$jobArr(ID)</td>
  <td>$jobArr(STATE)</td>
  <td>$jobArr(TYPE)</td>
  <td>$jobArr(REQ)</td>
  <td>$jobArr(SCRIPT)</td>
  <td>$jobArr(RESULTS)</td>
  <td align=\"right\">$jobArr(TIME) ms</td>
  <td>$jobArr(START_TIME)</td>
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

    return [rc.success]
}

# ------------------------------------------------------------------------------
# Enqueue jobs
#
proc enqueueJobs {queueId jobListVar {detached 0}} {

    upvar $jobListVar jobList
    set jobList [list]

    outputMsg "Enqueuing jobs ...<br>"
    if {[catch {
        for {set i 0} {$i < 10} {incr i} {
            if {$detached} {
                lappend jobList [ns_job queue -detached $queueId "foo $i"]
            } else {
                lappend jobList [ns_job queue $queueId "foo $i"]
            }
        }
    } err]} {
	global errorInfo
	set savedInfo $errorInfo
	outputMsg "Failed to enqueue jobs. Error Info: $errorInfo"
        return [rc.failure]
    }
    outputMsg "Done enqueuing jobs.<br><br>"

    return [rc.success]
}



# ------------------------------------------------------------------------------
# Enqueue jobs
#
proc enqueueLongJobs {queueId jobListVar {detached 0}} {

    upvar $jobListVar jobList
    set jobList [list]

    outputMsg "Enqueuing jobs ...<br>"
    if {[catch {
        for {set i 0} {$i < 10} {incr i} {
            if {$detached} {
                lappend jobList [ns_job queue -detached $queueId "fooLong $i"]
            } else {
                lappend jobList [ns_job queue $queueId "fooLong $i"]
            }
        }
    } err]} {
	global errorInfo
	set savedInfo $errorInfo
	outputMsg "Failed to enqueue jobs. Error Info: $errorInfo"
        return [rc.failure]
    }
    outputMsg "Done enqueuing jobs.<br><br>"

    return [rc.success]
}


# ------------------------------------------------------------------------------
# Wait for each job.
#
proc waitForEachJob {queueId jobList} {

    outputMsg "Wait for each job to complete ...<br><br><blockquote>"
    if {[catch {
        foreach jobId $jobList {
            outputMsg "Job Id: $jobId &nbsp;&nbsp; Job Result: [ns_job wait $queueId $jobId]<br>"
        }
    } err]} {
        global errorInfo
        set savedInfo $errorInfo
        outputMsg "Failed to wait on job. Error Info: $errorInfo"
        return [rc.failure]
    }
    outputMsg "</blockquote><br>Done removing the jobs.<br><br>"

    return [rc.success]
}


# ------------------------------------------------------------------------------
# Cancel jobs.
#
proc cancelJobs {queueId jobList} {

    outputMsg "Cancelling all the jobs...<br>"
    foreach jobId $jobList {
        if {[catch {
            ns_job cancel $queueId $jobId
        } err]} {
            global errorInfo
            set savedInfo $errorInfo
            outputMsg "Failed to cancel a job. Job ID: $jobId  errorInfo: $errorInfo<br>"
            return [rc.failure]
        }
    }
    outputMsg "Done cancelling all the jobs.<br><br>"

    return [rc.success]
}


# ------------------------------------------------------------------------------
# Unit Test #1
#
proc ns_job_unit_test_1 {testNum} {

    global queueId

    set count 10

    outputMsg "<br>Running test # $testNum.<br><br>"

    #
    # Enqueue jobs
    #
    if {[enqueueJobs $queueId jobList] != [rc.success]} {
        return [rc.failure]
    }
    
    #
    # Wait for each job to complete.
    #
    if {[waitForEachJob $queueId $jobList]} {
        return [rc.failure]
    }

    outputMsg "<br>Test # $testNum Complete.<br>"

    return [rc.success]
}


# ------------------------------------------------------------------------------
# Unit Test #2
#
proc ns_job_unit_test_2 {testNum} {

    global queueId

    outputMsg "<br>Running test # $testNum.<br><br>"

    #
    # Enqueue jobs
    #
    if {[enqueueJobs $queueId jobList] != [rc.success]} {
        return [rc.failure]
    }
    
    #
    # Watch jobs progress.
    #
    watchQueue $queueId $jobList

    #
    # Remove the jobs from the queue.
    #
    if {[waitForEachJob $queueId $jobList]} {
        return [rc.failure]
    }

    outputMsg "<br>Test # $testNum Complete.<br>"
    
    return [rc.success]
}


# ------------------------------------------------------------------------------
# Unit Test #3
#
proc ns_job_unit_test_3 {testNum} {

    global queueId

    outputMsg "<br>Running test # $testNum.<br><br>"

    #
    # Enqueue jobs
    #
    if {[enqueueJobs $queueId jobList] != [rc.success]} {
        return [rc.failure]
    }
    
    #
    # Wait for any job to complete.
    #
    outputMsg "Wait for any job to complete...<br>"
    if {[catch {
        ns_job wait_any $queueId

    } err]} {
        global errorInfo
        set savedInfo $errorInfo
        outputMsg "wait_any failed. Error Info: $errorInfo"
        return [rc.failure]
    }
    outputMsg "Done waiting for any job to complete.<br><br>"

    #
    # Remove the jobs from the queue.
    # 
    if {[waitForEachJob $queueId $jobList]} {
        return [rc.failure]
    }

    #
    # Wait for any job to complete.
    #
    outputMsg "Wait again for any job (this should not hang) ...<br>"
    if {[catch {
        ns_job wait_any $queueId

    } err]} {
        global errorInfo
        set savedInfo $errorInfo
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

    global queueId

    outputMsg "<br>Running test # $testNum.<br><br>"
    #
    # Enqueue jobs
    #
    if {[enqueueJobs $queueId jobList [true]] != [rc.success]} {
        return [rc.failure]
    }

    #
    # Watch jobs progress.
    #
    watchQueue $queueId $jobList

    #
    # Test wait
    #
    foreach jobId $jobList {
        if {[catch {
            ns_job wait $queueId $jobId
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

    global queueId

    outputMsg "<br>Running test # $testNum.<br><br>"
    #
    # Enqueue jobs
    #
    if {[enqueueJobs $queueId jobList] != [rc.success]} {
        return [rc.failure]
    }
    
    #
    # Wait for any job to complete.
    #
    outputMsg "Wait for any job to complete...<br>"
    if {[catch {
        ns_job wait_any $queueId

    } err]} {
        global errorInfo
        set savedInfo $errorInfo
        outputMsg "wait_any failed. Error Info: $errorInfo"
        return [rc.failure]
    }
    outputMsg "Done waiting for any job to complete.<br><br>"


    #
    # Cancel all the jobs
    #
    if {[cancelJobs $queueId $jobList] != [rc.success]} {
        return [rc.failure]
    }

    #
    # Test wait
    #
    foreach jobId $jobList {
        if {[catch {
            ns_job wait $queueId $jobId
            outputMsg "Failure: Wait should not be allowed for cancelled jobs.<br>"
            return [rc.failure]
        } err]} {
            # wait should have thrown an error.
        }
    }

    #
    # Watch jobs progress.
    #
    watchQueue $queueId $jobList

    outputMsg "<br>Test # $testNum Complete.<br>"
    return [rc.success]
}


# ------------------------------------------------------------------------------
# Unit Test #6
#
proc ns_job_unit_test_6 {testNum} {

    global queueId

    outputMsg "<br>Running test # $testNum.<br><br>"

    #
    # Enqueue jobs
    #
    if {[enqueueLongJobs $queueId jobList] != [rc.success]} {
        return [rc.failure]
    }
    
    #
    # Wait for any job to complete.
    #
    outputMsg "Wait for any job to complete...<br>"
    if {[catch {
        ns_job wait_any -timeout 0 10 $queueId
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
    if {[cancelJobs $queueId $jobList] != [rc.success]} {
        return [rc.failure]
    }


    #
    # Enqueue jobs
    #
    outputMsg "Enqueuing jobs ...<br>"
    set jobList [list]
    if {[enqueueLongJobs $queueId jobList] != [rc.success]} {
        return [rc.failure]
    }

    #
    # Test wait
    #
    foreach jobId $jobList {
        if {[catch {
            ns_job wait -timeout 0 10 $queueId $jobId
            outputMsg "Failed to timeout call."
            return [rc.failure]
        } err]} {
            # wait should have thrown an error.
        }
    }

    #
    # Cancel all the jobs
    #
    if {[cancelJobs $queueId $jobList] != [rc.success]} {
        return [rc.failure]
    }

    outputMsg "<br>Test # $testNum Complete.<br>"
    return [rc.success]
}



# ------------------------------------------------------------------------------
# Unit Test #7
#
proc ns_job_unit_test_7 {testNum} {

    global queueId

    outputMsg "<br>Running test # $testNum.<br><br>"

    set queueId_1 [ns_job genID]
    set queueId_1_return [ns_job create -description "queueId_1" $queueId_1]

    
    displayQueues

    set queueId_2 [ns_job genID]
    set queueId_2_return [ns_job create -description "queueId_2" $queueId_2]

    #
    # Enqueue jobs
    #
    if {[enqueueJobs $queueId_1 jobList] != [rc.success]} {
        return [rc.failure]
    }

    #
    # Wait for each job to complete.
    #
    if {[waitForEachJob $queueId_1_return $jobList]} {
        return [rc.failure]
    }


    #
    # Enqueue jobs
    #
    if {[enqueueJobs $queueId_2_return jobList] != [rc.success]} {
        return [rc.failure]
    }

    #
    # Wait for each job to complete.
    #
    if {[waitForEachJob $queueId_2_return $jobList]} {
        return [rc.failure]
    }


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
<li><a href=ns_job.adp?testSelected=7>Test \#6 - Generate Queue ID</a>
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
  <th>Name</th>
  <th>Desc</th>
  <th>MAX Threads</th>
  <th>Number Threads</th>
  <th>Number Idle</th>
  <th>Request</th>
</tr>
"

    foreach queue $queueList {
        array set queueArr $queue
        
        outputMsg "
<tr>
  <td>$queueArr(NAME)</td>
  <td>$queueArr(DESC)</td>
  <td>$queueArr(MAX_THREADS)</td>
  <td>$queueArr(NUM_THREADS)</td>
  <td>$queueArr(NUM_IDLE)</td>
  <td>$queueArr(REQ)</td>
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

if {[createQueue $queueId] == [rc.failure]} {
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
    7 {
        if {[ns_job_unit_test_7 $testNum] != [rc.success]} {
            outputMsg "<br>Test $testNum failed!<br>"
        }
    }
    default {
        outputMsg "Unknown option. $testNum"
    }
}

%>
