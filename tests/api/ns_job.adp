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
# Write the message to the page.
#
proc outputMsg {msg} {
    ns_adp_puts $msg
}


# ------------------------------------------------------------------------------
# Write the message to the page and the log.
#
proc outputMsgLog {msg} {
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
    while {$x < 100000} {
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
            outputMsgLog "Failure: Create queue should have thrown an erorr.<br>"
            return [rc.failure]
        } err]} {
            return [rc.success]
        }
    } else {
        outputMsgLog "Creating thread pool queue with max thread: $maxThreads ...<br>"
        ns_job create -desc "Unit Test Queue \#1" $queueId $maxThreads
        outputMsgLog "Done creating thread pool queue.<br><br>"

        if {![queueExists $queueId]} {
            outputMsgLog "Failure: Failed to create queue.<br>"
            return [rc.failure]
        }
    }
    return [rc.success]
}


# ------------------------------------------------------------------------------
# Display Queues
#
proc displayThreadPool {} {
    if {[catch {
         set threadList [ns_job threadlist]
     } err]} {
	global errorInfo
	set savedInfo $errorInfo
        outputMsgLog "Failed to get thread pool list. Error Info: $errorInfo<br>"
        return [rc.failure]
    }

    array set threadArr $threadList

    outputMsg "\
<br>
<table border=1>
<tr>
  <th colspan=4> ns_job thread pool </th>
</tr>
<tr>
  <th>maxthreads</th>
  <th>numthread</th>
  <th>numidle</th>
  <th>req</th>
</tr>
"
    outputMsg "
<tr>
  <td>$threadArr(maxthreads)</td>
  <td>$threadArr(numthreads)</td>
  <td>$threadArr(numidle)</td>
  <td>$threadArr(req)</td>
</tr>
"
    outputMsg "\
</table>
<br>
"
}


# ------------------------------------------------------------------------------
# Display Queues
#
proc displayQueues {} {
    if {[catch {
         set queueList [ns_job queuelist]
     } err]} {
	global errorInfo
	set savedInfo $errorInfo
        outputMsgLog "Failed to get queue list. Error Info: $errorInfo<br>"
        return [rc.failure]
    }

        outputMsg "\
<br>
<table border=1>
<tr>
  <th colspan=5> ns_job queue list </th>
</tr>
<tr>
  <th>name</th>
  <th>desc</th>
  <th>maxthread</th>
  <th>numrunning</th>
  <th>req</th>
</tr>
"

    foreach queue $queueList {
        array set queueArr $queue
        
        outputMsg "
<tr>
  <td>$queueArr(name)</td>
  <td>$queueArr(desc)</td>
  <td>$queueArr(maxthreads)</td>
  <td>$queueArr(numrunning)</td>
  <td>$queueArr(req)</td>
</tr>
"
    }

        outputMsg "\
</table>
<br>
"
}


# ------------------------------------------------------------------------------
# Watch queue jobs progress.
#
proc watchJobs {queueId} {

    outputMsgLog "Watch $queueId jobs progress ...<br><br>"

    set done 0
    while {$done == 0} {
        set done 1
        
        set first [true]
        
        if {[catch {
            set jobList [ns_job joblist $queueId]
        } err]} {
            global errorInfo
            set savedInfo $errorInfo
            outputMsgLog "Failed to get job list info. Error Info: $errorInfo"
            return [rc.failure]
        }

        foreach job $jobList {
            array set jobArr $job
            
            if {$first} {
                outputMsg "
<br>
<table border=1>
<tr>
  <th colspan=8> $queueId </th>
<tr>
<tr>
  <th>id</th>
  <th>state</th>
  <th>type</th>
  <th>request</th>
  <th>script</th>
  <th>results</th>
  <th>time</th>
  <th>starttime</th>
  <th>endtime</th>
</tr>
"
                set first [false]
            }
            
            outputMsg "
<tr>
  <td>$jobArr(id)</td>
  <td>$jobArr(state)</td>
  <td>$jobArr(type)</td>
  <td>$jobArr(req)</td>
  <td>$jobArr(script)</td>
  <td>$jobArr(results)</td>
  <td align=\"right\">$jobArr(time) ms</td>
  <td>[clock format $jobArr(starttime) -format {%D %T}]</td>
  <td>[clock format $jobArr(endtime) -format {%D %T}]</td>
</tr>
"

            #
            # Keep watching if any jobs are queued or running.
            #
            if {($jobArr(state) == "scheduled") ||
                ($jobArr(state) == "running") } {
                set done 0
            }   
        }
        outputMsg "
</table>
<br>
"
        displayThreadPool
        ns_sleep 1
    }

    return [rc.success]
}


# ------------------------------------------------------------------------------
# Watch queue jobs progress.
#
proc watchAllJobs {} {

    outputMsgLog "<hr>Watch all queue's jobs progress ...<br><br>"

    set done 0
    while {$done == 0} {
        set done 1

        if {[catch {
            set queueList [ns_job queues]
        } err]} {
            global errorInfo
            set savedInfo $errorInfo
            outputMsgLog "Failed to get queue list. Error Info: $errorInfo"
            return [rc.failure]
        }

        foreach queueId $queueList {

            set first [true]
            if {[catch {
                set jobList [ns_job joblist $queueId]
            } err]} {
                global errorInfo
                set savedInfo $errorInfo
                outputMsgLog "Failed to get job list info. Error Info: $errorInfo"
                return [rc.failure]
            }

            foreach job $jobList {
                array set jobArr $job
                
                if {$first} {
                    outputMsg "
<br>
<table border=1>
<tr>
  <th colspan=8> $queueId </th>
<tr>
<tr>
  <th>id</th>
  <th>state</th>
  <th>type</th>
  <th>request</th>
  <th>script</th>
  <th>results</th>
  <th>time</th>
  <th>starttime</th>
  <th>endtime</th>
</tr>
"
                    set first [false]
                }
                    
                outputMsg "
<tr>
  <td>$jobArr(id)</td>
  <td>$jobArr(state)</td>
  <td>$jobArr(type)</td>
  <td>$jobArr(req)</td>
  <td>$jobArr(script)</td>
  <td>$jobArr(results)</td>
  <td align=\"right\">$jobArr(time) ms</td>
  <td>[clock format $jobArr(starttime) -format {%D %T}]</td>
  <td>[clock format $jobArr(endtime) -format {%D %T}]</td>
</tr>
"
                #
                # Keep watching if any jobs are queued or running.
                #
                if {($jobArr(state) == "scheduled") ||
                    ($jobArr(state) == "running") } {
                    set done 0
                }   
            }
        }
        outputMsg "
</table>
<br>
<br>
"
        displayThreadPool
        ns_sleep 1

        outputMsg "
<hr>
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

    outputMsgLog "Enqueuing jobs ...<br>"
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
	outputMsgLog "Failed to enqueue jobs. Error Info: $errorInfo"
        return [rc.failure]
    }
    outputMsgLog "Done enqueuing jobs.<br><br>"

    return [rc.success]
}


# ------------------------------------------------------------------------------
# Enqueue jobs
#
proc enqueueLongJobs {queueId jobListVar {detached 0}} {

    upvar $jobListVar jobList
    set jobList [list]

    outputMsgLog "Enqueuing jobs ...<br>"
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
	outputMsgLog "Failed to enqueue jobs. Error Info: $errorInfo"
        return [rc.failure]
    }
    outputMsgLog "Done enqueuing jobs.<br><br>"

    return [rc.success]
}


# ------------------------------------------------------------------------------
# Wait for each job.
#
proc waitForEachJob {queueId jobList} {

    outputMsgLog "Wait for each job to complete ...<br><br><blockquote>"
    if {[catch {
        foreach jobId $jobList {
            outputMsgLog "Job Id: $jobId &nbsp;&nbsp; \
                          Job Result: [ns_job wait $queueId $jobId]<br>"
        }
    } err]} {
        global errorInfo
        set savedInfo $errorInfo
        outputMsgLog "Failed to wait on job. Error Info: $errorInfo"
        return [rc.failure]
    }
    outputMsgLog "</blockquote><br>Done removing the jobs.<br><br>"

    return [rc.success]
}


# ------------------------------------------------------------------------------
# busy wait for all jobs to complete.
#
proc busyWait {queueId} {

    set done 0
    while {$done == 0} {
        set done 1

        set jobList [ns_job joblist $queueId]

        foreach job $jobList {
            array set jobArr $job
            
            if {($jobArr(state) == "scheduled") ||
                ($jobArr(state) == "running") } {
                set done 0
            }   
        }
    }
}


# ------------------------------------------------------------------------------
# Cancel jobs.
#
proc cancelJobs {queueId jobList} {

    outputMsgLog "Cancelling all the jobs...<br>"
    foreach jobId $jobList {
        if {[catch {
            ns_job cancel $queueId $jobId
        } err]} {
            global errorInfo
            set savedInfo $errorInfo
            outputMsgLog "Failed to cancel a job. \
                          Job ID: $jobId  errorInfo: $errorInfo<br>"
            return [rc.failure]
        }
    }
    outputMsgLog "Done cancelling all the jobs.<br><br>"

    return [rc.success]
}


# ------------------------------------------------------------------------------
# Unit Test #1
#
proc ns_job_unit_test_1 {testNum} {

    global queueId

    set count 10

    outputMsgLog "<br>Running test # $testNum.<br><br>"

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

    return [rc.success]
}


# ------------------------------------------------------------------------------
# Unit Test #2
#
proc ns_job_unit_test_2 {testNum} {

    global queueId

    outputMsgLog "<br>Running test # $testNum.<br><br>"

    #
    # Enqueue jobs
    #
    if {[enqueueJobs $queueId jobList] != [rc.success]} {
        return [rc.failure]
    }
    
    #
    # Watch jobs progress.
    #
    watchJobs $queueId

    #
    # Remove the jobs from the queue.
    #
    if {[waitForEachJob $queueId $jobList]} {
        return [rc.failure]
    }

    return [rc.success]
}


# ------------------------------------------------------------------------------
# Unit Test #3
#
proc ns_job_unit_test_3 {testNum} {

    global queueId

    outputMsgLog "<br>Running test # $testNum.<br><br>"

    #
    # Enqueue jobs
    #
    if {[enqueueJobs $queueId jobList] != [rc.success]} {
        return [rc.failure]
    }
    
    #
    # Wait for any job to complete.
    #
    outputMsgLog "Wait for any job to complete...<br>"
    if {[catch {
        ns_job waitany $queueId

    } err]} {
        global errorInfo
        set savedInfo $errorInfo
        outputMsgLog "waitany failed. Error Info: $errorInfo"
        return [rc.failure]
    }
    outputMsgLog "Done waiting for any job to complete.<br><br>"

    #
    # Remove the jobs from the queue.
    # 
    if {[waitForEachJob $queueId $jobList]} {
        return [rc.failure]
    }

    #
    # Wait for any job to complete.
    #
    outputMsgLog "Wait again for any job (this should not hang) ...<br>"
    if {[catch {
        ns_job waitany $queueId

    } err]} {
        global errorInfo
        set savedInfo $errorInfo
        outputMsgLog "waitany failed. Error Info: $errorInfo"
        return [rc.failure]
    }
    outputMsgLog "Done waiting for any job to complete.<br><br>"

    return [rc.success]
}


# ------------------------------------------------------------------------------
# Unit Test #4
#
proc ns_job_unit_test_4 {testNum} {

    set queueId [ns_job create [ns_job genid]]

    outputMsgLog "<br>Running test # $testNum.<br><br>"
    #
    # Enqueue jobs
    #
    if {[enqueueJobs $queueId jobList [true]] != [rc.success]} {
        return [rc.failure]
    }

    #
    # Watch jobs progress.
    #
    watchJobs $queueId

    #
    # Test wait
    #
    foreach jobId $jobList {
        if {[catch {
            ns_job wait $queueId $jobId
            outputMsgLog "Failure: Wait should not be allowed for detached jobs.<br>"
            return [rc.failure]
        } err]} {
            # wait should have thrown an error.
        }
    }

    ns_job delete $queueId

    return [rc.success]
}


# ------------------------------------------------------------------------------
# Unit Test #5
#
proc ns_job_unit_test_5 {testNum} {

    global queueId

    outputMsgLog "<br>Running test # $testNum.<br><br>"
    #
    # Enqueue jobs
    #
    if {[enqueueJobs $queueId jobList] != [rc.success]} {
        return [rc.failure]
    }
    
    #
    # Wait for any job to complete.
    #
    outputMsgLog "Wait for any job to complete...<br>"
    if {[catch {
        ns_job waitany $queueId

    } err]} {
        global errorInfo
        set savedInfo $errorInfo
        outputMsgLog "waitany failed. Error Info: $errorInfo"
        return [rc.failure]
    }
    outputMsgLog "Done waiting for any job to complete.<br><br>"


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
            outputMsgLog "Failure: Wait should not be allowed for cancelled jobs.<br>"
            return [rc.failure]
        } err]} {
            # wait should have thrown an error.
        }
    }

    #
    # Enqueue jobs
    #
    if {[enqueueJobs $queueId jobList] != [rc.success]} {
        return [rc.failure]
    }
    
    #
    # Watch jobs progress.
    #
    busyWait $queueId

    #
    # Cancel all the jobs
    #
    if {[cancelJobs $queueId $jobList] != [rc.success]} {
        return [rc.failure]
    }

    return [rc.success]
}


# ------------------------------------------------------------------------------
# Unit Test #6
#
proc ns_job_unit_test_6 {testNum} {

    global queueId

    outputMsgLog "<br>Running test # $testNum.<br><br>"

    #
    # Enqueue jobs
    #
    if {[enqueueLongJobs $queueId jobList] != [rc.success]} {
        return [rc.failure]
    }
    
    #
    # Wait for any job to complete.
    #
    outputMsgLog "Wait for any job to complete...<br>"
    if {[catch {
        ns_job waitany -timeout 0:10000 $queueId
        outputMsgLog "Failed to timeout call."
        return [rc.failure]
    } err]} {
        #
        # When a message times out, it throws an error.
        #
    }
    outputMsgLog "Done waiting for any job to complete.<br><br>"

    #
    # Cancel all the jobs
    #
    if {[cancelJobs $queueId $jobList] != [rc.success]} {
        return [rc.failure]
    }


    #
    # Enqueue jobs
    #
    outputMsgLog "Enqueuing jobs ...<br>"
    if {[enqueueLongJobs $queueId jobList] != [rc.success]} {
        return [rc.failure]
    }

    #
    # Test wait
    #
    foreach jobId $jobList {
        if {[catch {
            ns_job wait -timeout 0:10000 $queueId $jobId
            outputMsgLog "Failed to timeout call."
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

    return [rc.success]
}



# ------------------------------------------------------------------------------
# Unit Test #7
#
proc ns_job_unit_test_7 {testNum} {

    global queueId

    outputMsgLog "<br>Running test # $testNum.<br><br>"

    set queueId_1 [ns_job genid]
    set queueId_1_return [ns_job create -description "queueId_1" $queueId_1]
    
    set queueId_2 [ns_job genid]
    set queueId_2_return [ns_job create -description "queueId_2" $queueId_2]

    #
    # Enqueue jobs
    #
    if {[enqueueLongJobs $queueId_1 jobList_1] != [rc.success]} {
        return [rc.failure]
    }

    #
    # Enqueue jobs
    #
    if {[enqueueJobs $queueId_2_return jobList_2] != [rc.success]} {
        return [rc.failure]
    }

    watchAllJobs 

    #
    # Wait for each job to complete.
    #
    if {[waitForEachJob $queueId_2_return $jobList_2]} {
        return [rc.failure]
    }

    #
    # Wait for each job to complete.
    #
    if {[waitForEachJob $queueId_1_return $jobList_1]} {
        return [rc.failure]
    }


    ns_job delete $queueId_1
    ns_job delete $queueId_2_return

    return [rc.success]
}


# ------------------------------------------------------------------------------
# Unit Test #8
#
proc ns_job_unit_test_8 {testNum} {

    global queueId

    outputMsgLog "<br>Running test # $testNum.<br><br>"

    set queueId_1 [ns_job genid]

    outputMsgLog "Creating new queue: $queueId_1 ...<br>"
    set queueId_1_return [ns_job create -description "queueId_1" $queueId_1]
    
    set queueList [ns_job queuelist]

    set found [false]
    foreach queue $queueList {
        array set queueArr $queue
        
        if {[string match $queueArr(name) $queueId_1]} {
            set found [true]
        }
    }
    
    if {!$found} {
        outputMsgLog "Failed to create a new queue"
        return [rc.failure]
    }
    outputMsgLog "Done creating queue.<br><br>"


    outputMsgLog "Deleting queue: $queueId_1 ...<br>"
    ns_job delete $queueId_1

    set queueList [ns_job queuelist]
    set found [false]
    foreach queue $queueList {
        array set queueArr $queue
        
        if {[string match $queueArr(name) $queueId_1]} {
            set found [true]
        }
    }

    if {$found} {
        outputMsgLog "Failed to delete queue. Queue Id: $queueId_1"
        return [rc.failure]
    }
    outputMsgLog "Done deleting queue.<br><br>"

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
<li><a href=ns_job.adp>Menu</a>
<li><a href=ns_job.adp?testSelected=1>Test \#1 - Basic funcationality</a>
<li><a href=ns_job.adp?testSelected=2>Test \#2 - List Jobs</a>
<li><a href=ns_job.adp?testSelected=3>Test \#3 - Wait Any</a>
<li><a href=ns_job.adp?testSelected=4>Test \#4 - Detached</a>
<li><a href=ns_job.adp?testSelected=5>Test \#5 - Cancel</a>
<li><a href=ns_job.adp?testSelected=6>Test \#6 - Timeout</a>
<li><a href=ns_job.adp?testSelected=7>Test \#7 - Generate Queue ID</a>
<li><a href=ns_job.adp?testSelected=8>Test \#8 - Delete Queue</a>
</ul>
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
    outputMsgLog "Failed to create queue"
    return [rc.failure]
}

displayThreadPool
displayQueues

set testNum [getTestNum]

switch -glob -- $testNum {

    0  {
        
    }
    1  {
        if {[ns_job_unit_test_1 $testNum] != [rc.success]} {
            outputMsgLog "<br>Test $testNum failed!<br>"
        } else {
            outputMsgLog "<br>Test # $testNum Complete.<br>"
        }
    }
    2  {
        if {[ns_job_unit_test_2 $testNum] != [rc.success]} {
            outputMsgLog "<br>Test $testNum failed!<br>"
        } else {
            outputMsgLog "<br>Test # $testNum Complete.<br>"
        }
    }
    3 {
        if {[ns_job_unit_test_3 $testNum] != [rc.success]} {
            outputMsgLog "<br>Test $testNum failed!<br>"
        } else {
            outputMsgLog "<br>Test # $testNum Complete.<br>"
        }
    }
    4 {
        if {[ns_job_unit_test_4 $testNum] != [rc.success]} {
            outputMsgLog "<br>Test $testNum failed!<br>"
        } else {
            outputMsgLog "<br>Test # $testNum Complete.<br>"
        }
    }
    5 {
        if {[ns_job_unit_test_5 $testNum] != [rc.success]} {
            outputMsgLog "<br>Test $testNum failed!<br>"
        } else {
            outputMsgLog "<br>Test # $testNum Complete.<br>"
        }
    }
    6 {
        if {[ns_job_unit_test_6 $testNum] != [rc.success]} {
            outputMsgLog "<br>Test $testNum failed!<br>"
        } else {
            outputMsgLog "<br>Test # $testNum Complete.<br>"
        }
    }
    7 {
        if {[ns_job_unit_test_7 $testNum] != [rc.success]} {
            outputMsgLog "<br>Test $testNum failed!<br>"
        } else {
            outputMsgLog "<br>Test # $testNum Complete.<br>"
        }
    }
    8 {
        if {[ns_job_unit_test_8 $testNum] != [rc.success]} {
            outputMsgLog "<br>Test $testNum failed!<br>"
        } else {
            outputMsgLog "<br>Test # $testNum Complete.<br>"
        }
    }
    default {
        outputMsgLog "Unknown option. $testNum"
    }
}

%>
